#include "pch.h"
#include "EditorLayer.h"
#include "Core/Application.h"
#include "Core/Profiler.h"
#include "Renderer/Core/Renderer.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include "ImGui/ImGuiUtils.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ScenePropertiesPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/AssetEditor.h"
namespace cerberus
{
	namespace
	{
		bool RayIntersectsBounds(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
			const glm::vec3& boundsMin, const glm::vec3& boundsMax)
		{
			float nearDistance = 0.0f;
			float farDistance = std::numeric_limits<float>::max();
			constexpr float epsilon = 1.0e-8f;

			for (uint32_t axis = 0; axis < 3; ++axis)
			{
				if (std::abs(rayDirection[axis]) < epsilon)
				{
					if (rayOrigin[axis] < boundsMin[axis] || rayOrigin[axis] > boundsMax[axis])
						return false;
					continue;
				}

				const float inverseDirection = 1.0f / rayDirection[axis];
				float firstDistance = (boundsMin[axis] - rayOrigin[axis]) * inverseDirection;
				float secondDistance = (boundsMax[axis] - rayOrigin[axis]) * inverseDirection;
				if (firstDistance > secondDistance)
					std::swap(firstDistance, secondDistance);

				nearDistance = glm::max(nearDistance, firstDistance);
				farDistance = glm::min(farDistance, secondDistance);
				if (nearDistance > farDistance)
					return false;
			}

			return farDistance >= 0.0f;
		}

		bool RayIntersectsTriangle(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
			const glm::vec3& vertex0, const glm::vec3& vertex1, const glm::vec3& vertex2,
			float& hitDistance)
		{
			constexpr float epsilon = 1.0e-7f;
			const glm::vec3 edge1 = vertex1 - vertex0;
			const glm::vec3 edge2 = vertex2 - vertex0;
			const glm::vec3 perpendicular = glm::cross(rayDirection, edge2);
			const float determinant = glm::dot(edge1, perpendicular);
			if (std::abs(determinant) < epsilon)
				return false;

			const float inverseDeterminant = 1.0f / determinant;
			const glm::vec3 vertexOffset = rayOrigin - vertex0;
			const float barycentricU = glm::dot(vertexOffset, perpendicular) * inverseDeterminant;
			if (barycentricU < 0.0f || barycentricU > 1.0f)
				return false;

			const glm::vec3 crossOffset = glm::cross(vertexOffset, edge1);
			const float barycentricV = glm::dot(rayDirection, crossOffset) * inverseDeterminant;
			if (barycentricV < 0.0f || barycentricU + barycentricV > 1.0f)
				return false;

			hitDistance = glm::dot(edge2, crossOffset) * inverseDeterminant;
			return hitDistance >= 0.0f;
		}
	}

	EditorLayer::EditorLayer()
		: Layer("EditorLayer")
	{
	}

	void EditorLayer::OnAttach()
	{
		AssetEditorManager::Init();
		auto resolution = Application::Get().GetWindow()->GetSize();
		m_Scene = AssetManager::GetAsset<Scene>("Resources/Scenes/First Scene.cscene");
		m_RendererViewportSize = {
			std::max(resolution.first, 1u),
			std::max(resolution.second, 1u)
		};
		m_PendingRendererType = m_Scene
			? m_Scene->GetRendererType()
			: SceneRendererType::PathTracer;
		m_RendererRecreationRequested = true;
		RecreateSceneRenderer();
		m_Camera = FlyingCamera(
			45.f,
			static_cast<float>(resolution.first) / static_cast<float>(std::max(resolution.second, 1u)),
			0.1f,
			1000.f);
		ApplyEditorCameraFromScene();


		Ref<ScenePropertiesPanel> scenePropertiesPanel = CreateRef<ScenePropertiesPanel>(m_Scene);
		Ref<SceneHierarchyPanel> sceneHierarchyPanel = CreateRef<SceneHierarchyPanel>(m_Scene);
		Ref<ContentBrowserPanel> contentBrowserPanel = CreateRef<ContentBrowserPanel>();

		AssetEditorManager::SetAssetModifiedCallback([this](const Ref<Asset>& asset)
		{
			if (!m_SceneRenderer || !asset || asset->GetAssetType() != AssetType::Material)
				return;

			m_SceneRenderer->OnMaterialAssetChanged(std::dynamic_pointer_cast<MaterialAsset>(asset));
		});

		scenePropertiesPanel->SetMaterialTableChangedCallback([this](RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable)
		{
			if (m_SceneRenderer)
				m_SceneRenderer->UpdateMeshMaterials(renderObjectID, materialTable);
		});
		scenePropertiesPanel->SetAssetFocusedCallback([this, contentBrowserPanel](AssetHandle assetHandle)
		{
			auto& panels = m_EditorPanelManager->GetPanels();
			auto contentBrowserIt = panels.find("ContentBrowserPanel");
			if (contentBrowserIt != panels.end())
				contentBrowserIt->second.IsOpen = true;

			contentBrowserPanel->FocusAsset(assetHandle);
		});

		sceneHierarchyPanel->SetSelectionChangedCallback(m_SelectionChangedCallback);
		sceneHierarchyPanel->SetEntityActivatedCallback([this](Entity entity)
		{
			FocusEntity(entity);
		});
		sceneHierarchyPanel->SetAssetDroppedOnEntityCallback([this](AssetHandle assetHandle, Entity target)
		{
			OnAssetDroppedOnEntity(assetHandle, target);
		});
		m_EditorPanelManager = CreateRef<EditorPanelManager>();
		m_EditorPanelManager->AddPanel("ScenePropertiesPanel", { true, scenePropertiesPanel });
		m_EditorPanelManager->AddPanel("SceneHierarchyPanel", { true, sceneHierarchyPanel });
		m_EditorPanelManager->AddPanel("ContentBrowserPanel", { true, contentBrowserPanel });
	}

	void EditorLayer::OnDetach()
	{
		// Persist the viewport camera pose with the scene so reopening it lands
		// back on the same vantage point.
		if (m_Scene)
		{
			StoreEditorCameraToScene();
			AssetImporter::Serialize(m_Scene);
		}

		AssetEditorManager::SetAssetModifiedCallback(nullptr);
		if (m_SceneRenderer)
		{
			Renderer::WaitIdleAndDrain();
			m_SceneRenderer.reset();
			Renderer::WaitIdleAndDrain();
		}
		m_EditorPanelManager->Shutdown();
		AssetEditorManager::Shutdown();
	}
	
	void EditorLayer::OnImGuiRender()
	{
		// Read the shortcut directly from ImGui so F11 remains global even when an
		// editor widget currently owns keyboard capture.
		if (ImGui::IsKeyPressed(Key::F11, false))
			m_IsViewportMaximized = !m_IsViewportMaximized;

		if (!m_IsViewportMaximized)
			UI_DockSpace();
		UI_Viewport();
		if (m_IsViewportMaximized)
			return;

		if (m_SceneRenderer)
			m_SceneRenderer->OnImGuiRender();
		Profiler::OnImGuiRender(Renderer::GetMainCommandBuffer());
		m_EditorPanelManager->OnImGuiRender();
		AssetEditorManager::OnImGuiRender();
	}

	void EditorLayer::OnEvent(Event& event)
	{
		m_Camera.OnEvent(event);
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(EditorLayer::OnWindowResize));
		dispatcher.Dispatch<KeyPressedEvent>(BIND_EVENT_FN(EditorLayer::OnKeyPress));
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		// A scene change requested from the event path waits until the current frame
		// has been presented. Keep showing the previous valid output in the meantime.
		if (m_RendererRecreationRequested || !m_Scene || !m_SceneRenderer)
			return;

		m_Camera.OnUpdate(ts);

		SceneRendererCamera sceneRendererCamera;
		sceneRendererCamera.Camera = m_Camera;
		sceneRendererCamera.ViewMatrix = m_Camera.GetViewMatrix();
		sceneRendererCamera.ViewPosition = m_Camera.GetPosition();
		sceneRendererCamera.Target = m_Camera.GetTarget();
		sceneRendererCamera.Near = m_Camera.GetNearPlane();
		sceneRendererCamera.Far = m_Camera.GetFarPlane();
		m_Scene->OnUpdate(*m_SceneRenderer, sceneRendererCamera, ts);
	}

	void EditorLayer::OnFrameEnd()
	{
		RecreateSceneRenderer();
	}

	void EditorLayer::RequestSceneRenderer(SceneRendererType rendererType)
	{
		if (!m_Scene)
			return;

		m_Scene->SetRendererType(rendererType);
		m_PendingRendererType = rendererType;
		m_RendererRecreationRequested = !m_SceneRenderer || rendererType != m_ActiveRendererType;
	}

	void EditorLayer::RecreateSceneRenderer()
	{
		if (!m_RendererRecreationRequested || !m_Scene)
			return;

		// This runs after Present, never inside a queued render command. First wait
		// for all native references to the old renderer. Then release and flush it
		// before constructing the replacement, so inactive resources consume no VRAM.
		if (m_SceneRenderer)
		{
			Renderer::WaitIdleAndDrain();
			m_SceneRenderer.reset();
			Renderer::WaitIdleAndDrain();
		}

		m_Scene->ResetRendererState();

		SceneRendererSpecification specification;
		specification.SwapChainTarget = false;
		specification.Resolution = {
			static_cast<int32_t>(glm::max(m_RendererViewportSize.x, 1u)),
			static_cast<int32_t>(glm::max(m_RendererViewportSize.y, 1u))
		};

		switch (m_PendingRendererType)
		{
		case SceneRendererType::Forward:
			m_SceneRenderer = CreateScope<ForwardSceneRenderer>(m_Scene, specification);
			break;
		case SceneRendererType::PathTracer:
			m_SceneRenderer = CreateScope<PathTracerSceneRenderer>(m_Scene, specification);
			break;
		}

		m_ActiveRendererType = m_PendingRendererType;
		m_RendererRecreationRequested = false;
		NV_CORE_INFO("Active SceneRenderer: {0}", std::string(SceneRendererTypeToString(m_ActiveRendererType)));
	}

	void EditorLayer::SelectEntities(const std::vector<Entity>& entities)
	{
		m_SelectedEntities = entities;
		m_EditorPanelManager->SetSelectionContext(entities);
	}

	void EditorLayer::DuplicateSelectedEntity()
	{
		if (!m_Scene || m_SelectedEntities.empty())
			return;

		std::vector<Entity> duplicates;
		for (Entity entity : m_SelectedEntities)
		{
			Entity duplicateEntity = m_Scene->DuplicateEntity(entity);
			if (duplicateEntity)
				duplicates.push_back(duplicateEntity);
		}
		if (!duplicates.empty())
			SelectEntities(duplicates);
	}

	void EditorLayer::DeleteSelectedEntities()
	{
		if (!m_Scene || m_SelectedEntities.empty())
			return;

		std::vector<Entity> entitiesToDelete = m_SelectedEntities;
		SelectEntities({});
		for (Entity entity : entitiesToDelete)
			m_Scene->DestroyEntity(entity);
	}

	void EditorLayer::FocusEntity(Entity entity)
	{
		if (!entity || !entity.HasComponent<TransformComponent>())
			return;

		auto& transformComponent = entity.GetComponent<TransformComponent>();
		const glm::mat4 entityTransform = transformComponent.GetTransform();
		glm::vec3 boundsMin(std::numeric_limits<float>::max());
		glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
		bool hasBounds = false;

		if (entity.HasComponent<MeshComponent>())
		{
			const auto& meshComponent = entity.GetComponent<MeshComponent>();
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshComponent.MeshHandle);
			Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
			if (meshSource)
			{
				const auto& vertices = meshSource->GetVertices();
				const auto& submeshes = meshSource->GetSubmeshes();
				for (const Submesh& submesh : submeshes)
				{
					const size_t firstVertex = submesh.BaseVertex;
					const size_t vertexEnd = std::min(firstVertex + submesh.VertexCount, vertices.size());
					const glm::mat4 worldTransform = entityTransform * submesh.Transform;
					for (size_t vertexIndex = firstVertex; vertexIndex < vertexEnd; ++vertexIndex)
					{
						const glm::vec3 worldPosition = glm::vec3(worldTransform * glm::vec4(vertices[vertexIndex].Position, 1.0f));
						boundsMin = glm::min(boundsMin, worldPosition);
						boundsMax = glm::max(boundsMax, worldPosition);
						hasBounds = true;
					}
				}
			}
		}

		glm::vec3 focusPoint = transformComponent.GetTranslation();
		float focusRadius = 1.0f;
		if (hasBounds)
		{
			focusPoint = (boundsMin + boundsMax) * 0.5f;
			focusRadius = glm::max(glm::length(boundsMax - focusPoint), 0.1f);
		}
		else
		{
			const glm::vec3 scale = glm::abs(transformComponent.GetScale());
			focusRadius = glm::max(glm::max(scale.x, scale.y), glm::max(scale.z, 0.1f));
		}

		m_Camera.SetFocusPoint(focusPoint, focusRadius);
	}

	bool EditorLayer::BuildViewportRay(const ImVec2& viewportPosition, const ImVec2& viewportSize, ViewportRay& ray) const
	{
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		const ImVec2 mousePosition = ImGui::GetMousePos();
		const float viewportX = (mousePosition.x - viewportPosition.x) / viewportSize.x;
		const float viewportY = (mousePosition.y - viewportPosition.y) / viewportSize.y;
		if (viewportX < 0.0f || viewportX > 1.0f || viewportY < 0.0f || viewportY > 1.0f)
			return false;

		// GLM is configured for Vulkan's [0, 1] depth range. The viewport image
		// is presented top-to-bottom, so screen Y must be flipped into NDC.
		const glm::vec2 ndc(viewportX * 2.0f - 1.0f, 1.0f - viewportY * 2.0f);
		const glm::mat4 inverseViewProjection = glm::inverse(m_Camera.GetViewProjectionMatrix());
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
		if (std::abs(farPoint.w) < 1.0e-8f)
			return false;

		farPoint /= farPoint.w;
		ray.Origin = m_Camera.GetPosition();
		const glm::vec3 direction = glm::vec3(farPoint) - ray.Origin;
		if (glm::dot(direction, direction) < 1.0e-12f)
			return false;

		ray.Direction = glm::normalize(direction);
		return true;
	}

	EditorLayer::MeshBounds EditorLayer::GetMeshBounds(const Ref<MeshSource>& meshSource)
	{
		if (!meshSource)
			return {};

		if (meshSource->Handle)
		{
			auto cachedBounds = m_MeshBoundsCache.find(meshSource->Handle);
			if (cachedBounds != m_MeshBoundsCache.end())
				return cachedBounds->second;
		}

		MeshBounds bounds;
		bounds.Min = glm::vec3(std::numeric_limits<float>::max());
		bounds.Max = glm::vec3(std::numeric_limits<float>::lowest());
		const auto& vertices = meshSource->GetVertices();
		std::vector<MeshBounds> submeshBounds;
		std::vector<MeshBounds> rawSubmeshBounds;
		submeshBounds.reserve(meshSource->GetSubmeshes().size());
		rawSubmeshBounds.reserve(meshSource->GetSubmeshes().size());
		for (const Submesh& submesh : meshSource->GetSubmeshes())
		{
			MeshBounds currentSubmeshBounds;
			currentSubmeshBounds.Min = glm::vec3(std::numeric_limits<float>::max());
			currentSubmeshBounds.Max = glm::vec3(std::numeric_limits<float>::lowest());
			MeshBounds currentRawSubmeshBounds;
			currentRawSubmeshBounds.Min = glm::vec3(std::numeric_limits<float>::max());
			currentRawSubmeshBounds.Max = glm::vec3(std::numeric_limits<float>::lowest());
			const size_t firstVertex = submesh.BaseVertex;
			const size_t vertexEnd = std::min(firstVertex + static_cast<size_t>(submesh.VertexCount), vertices.size());
			for (size_t vertexIndex = firstVertex; vertexIndex < vertexEnd; ++vertexIndex)
			{
				const glm::vec3& rawPosition = vertices[vertexIndex].Position;
				const glm::vec3 position = glm::vec3(submesh.Transform * glm::vec4(rawPosition, 1.0f));
				bounds.Min = glm::min(bounds.Min, position);
				bounds.Max = glm::max(bounds.Max, position);
				bounds.Valid = true;
				currentSubmeshBounds.Min = glm::min(currentSubmeshBounds.Min, position);
				currentSubmeshBounds.Max = glm::max(currentSubmeshBounds.Max, position);
				currentSubmeshBounds.Valid = true;
				currentRawSubmeshBounds.Min = glm::min(currentRawSubmeshBounds.Min, rawPosition);
				currentRawSubmeshBounds.Max = glm::max(currentRawSubmeshBounds.Max, rawPosition);
				currentRawSubmeshBounds.Valid = true;
			}
			submeshBounds.emplace_back(currentSubmeshBounds);
			rawSubmeshBounds.emplace_back(currentRawSubmeshBounds);
		}

		if (meshSource->Handle)
		{
			m_MeshBoundsCache[meshSource->Handle] = bounds;
			m_SubmeshBoundsCache[meshSource->Handle] = std::move(submeshBounds);
			m_RawSubmeshBoundsCache[meshSource->Handle] = std::move(rawSubmeshBounds);
		}
		return bounds;
	}

	glm::vec3 EditorLayer::GetEntityGizmoPivotOffset(Entity entity)
	{
		if (!entity || !entity.HasComponent<MeshComponent>())
			return glm::vec3(0.0f);

		const auto& meshComponent = entity.GetComponent<MeshComponent>();
		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshComponent.MeshHandle);
		Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
		if (!meshSource)
			return glm::vec3(0.0f);

		// A split entity's own TransformComponent already bakes in its
		// submesh's node Transform (see CreateSplitMeshEntitiesFromDrop), and
		// the renderer does not reapply it -- so the relevant bounds are the
		// *raw* submesh bounds, not the whole mesh's. A normal (unfiltered)
		// entity renders every submesh with its node Transform applied on top
		// of one shared entity transform, so the whole mesh's own bounds
		// (which already include each submesh's Transform) are what its local
		// space actually looks like.
		if (meshComponent.SubmeshIndex >= 0)
		{
			GetMeshBounds(meshSource); // ensures the raw cache below is populated
			const auto rawBoundsIt = m_RawSubmeshBoundsCache.find(meshSource->Handle);
			if (rawBoundsIt == m_RawSubmeshBoundsCache.end() ||
				(size_t)meshComponent.SubmeshIndex >= rawBoundsIt->second.size())
				return glm::vec3(0.0f);

			const MeshBounds& bounds = rawBoundsIt->second[meshComponent.SubmeshIndex];
			return bounds.Valid ? (bounds.Min + bounds.Max) * 0.5f : glm::vec3(0.0f);
		}

		const MeshBounds bounds = GetMeshBounds(meshSource);
		return bounds.Valid ? (bounds.Min + bounds.Max) * 0.5f : glm::vec3(0.0f);
	}

	bool EditorLayer::RaycastScene(const ViewportRay& ray, ViewportHit& closestHit)
	{
		if (!m_Scene)
			return false;

		bool foundHit = false;
		auto group = m_Scene->m_Registry.group<TransformComponent>(entt::get<MeshComponent>);
		for (const entt::entity entityID : group)
		{
			const auto& [transformComponent, meshComponent] = group.get<TransformComponent, MeshComponent>(entityID);
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshComponent.MeshHandle);
			Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
			if (!meshSource)
				continue;

			const glm::mat4 entityTransform = transformComponent.GetTransform();
			if (std::abs(glm::determinant(glm::mat3(entityTransform))) < 1.0e-8f)
				continue;

			const glm::mat4 inverseEntityTransform = glm::inverse(entityTransform);
			const glm::vec3 entityRayOrigin = glm::vec3(inverseEntityTransform * glm::vec4(ray.Origin, 1.0f));
			const glm::vec3 entityRayDirection = glm::mat3(inverseEntityTransform) * ray.Direction;
			const MeshBounds bounds = GetMeshBounds(meshSource);
			if (!bounds.Valid || !RayIntersectsBounds(entityRayOrigin, entityRayDirection, bounds.Min, bounds.Max))
				continue;

			const auto& vertices = meshSource->GetVertices();
			const auto& indices = meshSource->GetIndices();
			const auto submeshBoundsIt = m_SubmeshBoundsCache.find(meshSource->Handle);
			const auto& submeshes = meshSource->GetSubmeshes();
			for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex)
			{
				const Submesh& submesh = submeshes[submeshIndex];
				if (submeshBoundsIt != m_SubmeshBoundsCache.end() && submeshIndex < submeshBoundsIt->second.size())
				{
					const MeshBounds& submeshBounds = submeshBoundsIt->second[submeshIndex];
					if (!submeshBounds.Valid || !RayIntersectsBounds(
						entityRayOrigin, entityRayDirection, submeshBounds.Min, submeshBounds.Max))
						continue;
				}

				const glm::mat4 submeshWorldTransform = entityTransform * submesh.Transform;
				if (std::abs(glm::determinant(glm::mat3(submeshWorldTransform))) < 1.0e-8f)
					continue;

				const glm::mat4 inverseSubmeshTransform = glm::inverse(submeshWorldTransform);
				const glm::vec3 localRayOrigin = glm::vec3(inverseSubmeshTransform * glm::vec4(ray.Origin, 1.0f));
				const glm::vec3 localRayDirection = glm::mat3(inverseSubmeshTransform) * ray.Direction;
				const size_t firstTriangle = submesh.BaseIndex / 3;
				const size_t triangleEnd = std::min(firstTriangle + static_cast<size_t>(submesh.IndexCount / 3), indices.size());

				for (size_t triangleIndex = firstTriangle; triangleIndex < triangleEnd; ++triangleIndex)
				{
					const MeshSource::Index& triangle = indices[triangleIndex];
					const size_t vertexIndex0 = static_cast<size_t>(submesh.BaseVertex) + triangle.Index0;
					const size_t vertexIndex1 = static_cast<size_t>(submesh.BaseVertex) + triangle.Index1;
					const size_t vertexIndex2 = static_cast<size_t>(submesh.BaseVertex) + triangle.Index2;
					if (vertexIndex0 >= vertices.size() || vertexIndex1 >= vertices.size() || vertexIndex2 >= vertices.size())
						continue;

					float hitDistance = 0.0f;
					if (!RayIntersectsTriangle(localRayOrigin, localRayDirection,
						vertices[vertexIndex0].Position, vertices[vertexIndex1].Position, vertices[vertexIndex2].Position,
						hitDistance) || hitDistance >= closestHit.Distance)
						continue;

					closestHit.HitEntity = Entity(entityID, m_Scene.get());
					closestHit.Distance = hitDistance;
					closestHit.Position = ray.Origin + ray.Direction * hitDistance;
					closestHit.MaterialSlot = submesh.MaterialIndex;
					foundHit = true;
				}
			}
		}

		return foundHit;
	}

	AssetHandle EditorLayer::ResolveDroppedMesh(AssetHandle assetHandle)
	{
		const AssetMetadata& metadata = AssetManager::GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return AssetHandle(0);
		if (metadata.Type == AssetType::Mesh)
			return AssetManager::GetAsset<Mesh>(assetHandle) ? assetHandle : AssetHandle(0);
		if (metadata.Type != AssetType::MeshSource)
			return AssetHandle(0);

		// Raw FBX/GLTF/OBJ assets are sources, while scene components reference
		// reusable .cmesh assets. Create that lightweight wrapper once on demand.
		const std::filesystem::path meshPath = metadata.FilePath.parent_path() / (metadata.FilePath.stem().string() + ".cmesh");
		AssetHandle meshHandle = AssetManager::GetAssetHandleFromFilePath(meshPath);
		if (AssetManager::IsAssetHandleValid(meshHandle))
			return AssetManager::GetAsset<Mesh>(meshHandle) ? meshHandle : AssetHandle(0);

		Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(assetHandle);
		if (!meshSource)
			return AssetHandle(0);

		Ref<Mesh> mesh = AssetManager::CreateNewAsset<Mesh>(
			meshPath.filename().string(), meshPath.parent_path().string(), meshSource);
		if (!mesh)
			return AssetHandle(0);

		ContentBrowserPanel::Get().Refresh();
		return mesh->Handle;
	}

	glm::vec3 EditorLayer::ComputeDropPlacement(const ViewportRay& ray, const ViewportHit* surfaceHit) const
	{
		if (surfaceHit)
			return surfaceHit->Position;

		const float cameraTargetDistance = glm::distance(m_Camera.GetPosition(), m_Camera.GetTarget());
		const float placementDistance = glm::max(cameraTargetDistance, 5.0f);
		return ray.Origin + ray.Direction * placementDistance;
	}

	Entity EditorLayer::CreateMeshEntityFromDrop(AssetHandle meshHandle, const glm::vec3& placement, Entity parent)
	{
		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshHandle);
		if (!m_Scene || !mesh)
			return {};

		const AssetMetadata& metadata = AssetManager::GetMetadata(meshHandle);
		const std::string entityName = metadata.IsValid() ? metadata.FilePath.stem().string() : "Mesh";
		Entity entity = m_Scene->CreateEntity(entityName);
		entity.GetComponent<TransformComponent>().SetTranslation(placement);
		entity.AddComponent<MeshComponent>(meshHandle);
		if (parent)
			m_Scene->ParentEntity(entity, parent);
		SelectEntity(entity);
		return entity;
	}

	Entity EditorLayer::CreateSplitMeshEntitiesFromDrop(AssetHandle meshHandle, const glm::vec3& placement, Entity parent)
	{
		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshHandle);
		Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
		if (!m_Scene || !meshSource)
			return {};

		const AssetMetadata& metadata = AssetManager::GetMetadata(meshHandle);
		const std::string baseName = metadata.IsValid() ? metadata.FilePath.stem().string() : "Mesh";

		// Pure group node: no MeshComponent of its own, carries the drop
		// placement so every child (whose own local transform is the
		// submesh's baked node offset, set below) ends up exactly where the
		// whole mesh would have landed.
		Entity group = m_Scene->CreateEntity(baseName);
		group.GetComponent<TransformComponent>().SetTranslation(placement);
		if (parent)
			m_Scene->ParentEntity(group, parent);

		const auto& submeshes = meshSource->GetSubmeshes();
		for (uint32_t i = 0; i < submeshes.size(); i++)
		{
			const std::string childName = baseName + "_" + (submeshes[i].NodeName.empty()
				? std::to_string(i) : submeshes[i].NodeName);
			Entity child = m_Scene->CreateEntity(childName);
			// Bake the submesh's own node transform (its offset within the
			// imported model's node hierarchy) into the child's local
			// TransformComponent, rather than leaving it identity -- the
			// renderer skips re-applying it for a submesh-filtered instance
			// (RenderScene::RegisterMesh), so the entity's transform must be
			// the whole story or the gizmo desyncs from the rendered mesh.
			glm::vec3 translation, rotation, scale;
			math::DecomposeTransform(submeshes[i].Transform, translation, rotation, scale);
			auto& tc = child.GetComponent<TransformComponent>();
			tc.SetTranslation(translation);
			tc.SetRotation(glm::degrees(rotation));
			tc.SetScale(scale);
			auto& mc = child.AddComponent<MeshComponent>(meshHandle);
			mc.SubmeshIndex = (int32_t)i;
			m_Scene->ParentEntity(child, group);
		}

		SelectEntity(group);
		return group;
	}

	void EditorLayer::RenderSubmeshSplitPopup()
	{
		if (ImGui::BeginPopupModal("Split Into Submeshes?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (!m_PendingSplitMeshHandle)
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(m_PendingSplitMeshHandle);
			Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
			const size_t submeshCount = meshSource ? meshSource->GetSubmeshes().size() : 0;
			ImGui::Text("This mesh has %zu submeshes. Break it into separate entities?", submeshCount);

			const float columnWidth = ImGui::GetContentRegionAvailWidth() / 4;
			ImGui::Columns(4, 0, false);
			ImGui::SetColumnWidth(0, columnWidth);
			ImGui::SetColumnWidth(1, columnWidth);
			ImGui::SetColumnWidth(2, columnWidth);
			ImGui::SetColumnWidth(3, columnWidth);
			ImGui::NextColumn();

			if (ImGui::Button("Yes", ImVec2(columnWidth, 0)))
			{
				CreateSplitMeshEntitiesFromDrop(m_PendingSplitMeshHandle, m_PendingSplitPlacement, m_PendingSplitParent);
				m_PendingSplitMeshHandle = 0;
				m_PendingSplitParent = {};
				ImGui::CloseCurrentPopup();
			}

			ImGui::NextColumn();
			ImGui::SetItemDefaultFocus();
			if (ImGui::Button("No", ImVec2(columnWidth, 0)))
			{
				CreateMeshEntityFromDrop(m_PendingSplitMeshHandle, m_PendingSplitPlacement, m_PendingSplitParent);
				m_PendingSplitMeshHandle = 0;
				m_PendingSplitParent = {};
				ImGui::CloseCurrentPopup();
			}

			ImGui::NextColumn();
			ImGui::EndPopup();
		}
	}

	void EditorLayer::OnAssetDroppedOnEntity(AssetHandle assetHandle, Entity parent)
	{
		const AssetMetadata& metadata = AssetManager::GetMetadata(assetHandle);
		// Only meshes have a defined meaning here -- there's no viewport
		// surface hit to apply a Material to, and no scene-open action makes
		// sense for a hierarchy node.
		if (metadata.Type != AssetType::Mesh && metadata.Type != AssetType::MeshSource)
			return;

		const AssetHandle meshHandle = ResolveDroppedMesh(assetHandle);
		if (!meshHandle)
			return;

		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshHandle);
		Ref<MeshSource> meshSource = mesh ? mesh->GetMeshSource() : nullptr;
		if (meshSource && meshSource->GetSubmeshes().size() > 1)
		{
			m_PendingSplitMeshHandle = meshHandle;
			m_PendingSplitPlacement = glm::vec3(0.0f);
			m_PendingSplitParent = parent;
			ImGui::OpenPopup("Split Into Submeshes?");
		}
		else
		{
			CreateMeshEntityFromDrop(meshHandle, glm::vec3(0.0f), parent);
		}
	}

	bool EditorLayer::ApplyMaterialFromDrop(AssetHandle materialHandle, const ViewportHit& hit)
	{
		Entity hitEntity = hit.HitEntity;
		if (!hitEntity || !hitEntity.HasComponent<MeshComponent>() ||
			!AssetManager::GetAsset<MaterialAsset>(materialHandle))
			return false;

		auto& meshComponent = hitEntity.GetComponent<MeshComponent>();
		if (!meshComponent.MaterialTable)
		{
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(meshComponent.MeshHandle);
			meshComponent.MaterialTable = mesh
				? CreateRef<MaterialTable>(mesh->GetMaterialTable())
				: CreateRef<MaterialTable>();
		}

		meshComponent.MaterialTable->SetMaterial(hit.MaterialSlot, materialHandle);
		const RenderObjectID renderObjectID{ hitEntity.GetComponent<IDComponent>().ID };
		if (m_SceneRenderer)
			m_SceneRenderer->UpdateMeshMaterials(renderObjectID, meshComponent.MaterialTable);
		SelectEntity(hitEntity);
		return true;
	}

	void EditorLayer::NewScene()
	{
		m_Scene = CreateRef<Scene>("New Scene");
		m_PendingRendererType = m_Scene->GetRendererType();
		m_RendererRecreationRequested = true;
		SelectEntities({});
		m_EditorPanelManager->SetSceneContext(m_Scene);
	}

	void EditorLayer::OpenScene()
	{
		std::string filepath = FileDialogs::OpenFile("Cerberus Scene (*.cscene)\0*.cscene\0");
		if (!filepath.empty())
		{
			std::filesystem::path relativePath = FileSystem::GetRelativePathFromAbsolute(filepath);
			m_Scene = AssetManager::GetAsset<Scene>(relativePath);
			if (!m_Scene)
			{
				NV_CORE_ERROR("Failed to open scene: {0}", relativePath.string());
				return;
			}
			m_Scene->SetName(relativePath.stem().string());
			m_PendingRendererType = m_Scene->GetRendererType();
			m_RendererRecreationRequested = true;
			SelectEntities({});
			m_EditorPanelManager->SetSceneContext(m_Scene);
			ApplyEditorCameraFromScene();
		}
	}

	void EditorLayer::OpenScene(const Ref<Scene>& scene)
	{
		if (!scene)
			return;

		m_Scene = scene;
		m_PendingRendererType = m_Scene->GetRendererType();
		m_RendererRecreationRequested = true;
		SelectEntities({});
		m_EditorPanelManager->SetSceneContext(m_Scene);
		ApplyEditorCameraFromScene();
	}

	void EditorLayer::ApplyEditorCameraFromScene()
	{
		if (m_Scene && m_Scene->HasEditorCameraPose())
			m_Camera.SetPose(m_Scene->GetEditorCameraPosition(),
				m_Scene->GetEditorCameraYaw(), m_Scene->GetEditorCameraPitch());
	}

	void EditorLayer::StoreEditorCameraToScene()
	{
		if (m_Scene)
			m_Scene->SetEditorCameraPose(m_Camera.GetPosition(), m_Camera.GetYaw(), m_Camera.GetPitch());
	}

	void EditorLayer::SaveSceneAs()
	{
		std::string filepath = FileDialogs::SaveFile("Cerberus Scene (*.cscene)\0*.cscene\0");
		if (!filepath.empty())
		{
			std::filesystem::path relativePath = FileSystem::GetRelativePathFromAbsolute(filepath);

			if (AssetManager::GetMetadata(relativePath).IsValid())
			{
				AssetImporter::Serialize(m_Scene);
			}
			else
			{
				bool succeded = AssetManager::LoadMemoryAsset(m_Scene, relativePath.filename().string(), relativePath.parent_path().string());
				if (succeded)
					ContentBrowserPanel::Get().Refresh();
			}

		}
	}

	void EditorLayer::SaveScene()
	{
		AssetImporter::Serialize(m_Scene);
	}

	bool EditorLayer::OnWindowResize(WindowResizeEvent& event)
	{
		return false;
	}

	bool EditorLayer::OnKeyPress(KeyPressedEvent& event)
	{
		bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		switch (event.GetKeyCode())
		{
		// Scene keys
		case Key::N:
		{
			if (control)
				NewScene();
			break;
		}
		case Key::O:
		{
			if (control)
				OpenScene();
			break;
		}
		case Key::S:
		{
			if (control)
			{
				if (shift)
					SaveSceneAs();
				else
					SaveScene();
			}
			break;
		}
		case Key::D:
		{
			if (control)
				DuplicateSelectedEntity();
			break;
		}
		case Key::Delete:
			DeleteSelectedEntities();
			break;
		// Gizmos
		case Key::Q:
			m_GizmoType = -1;
			break;
		case Key::W:
			m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
			break;
		case Key::E:
			m_GizmoType = ImGuizmo::OPERATION::ROTATE;
			break;
		case Key::R:
			m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;
		}
		return false;
	}

	void EditorLayer::UI_RenderTittleBar()
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exits")) { Application::Get().Close(); }
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) { NewScene(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) { OpenScene(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) { SaveSceneAs(); }
				ImGui::Separator();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::MenuItem("Maximize Viewport", "F11"))
					m_IsViewportMaximized = true;
				ImGui::Separator();

				auto& panels = m_EditorPanelManager->GetPanels();
				auto panelToggle = [&panels](const char* label, const char* panelID)
				{
					auto panelIt = panels.find(panelID);
					if (panelIt != panels.end())
						ImGui::MenuItem(label, nullptr, &panelIt->second.IsOpen);
				};

				panelToggle("Scene Properties", "ScenePropertiesPanel");
				panelToggle("Scene Hierarchy", "SceneHierarchyPanel");
				panelToggle("Content Browser", "ContentBrowserPanel");

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Renderer"))
			{
				const SceneRendererType selectedRenderer = m_RendererRecreationRequested
					? m_PendingRendererType
					: m_ActiveRendererType;
				if (ImGui::MenuItem("Forward", nullptr, selectedRenderer == SceneRendererType::Forward))
					RequestSceneRenderer(SceneRendererType::Forward);
				if (ImGui::MenuItem("Path Tracer", nullptr, selectedRenderer == SceneRendererType::PathTracer))
					RequestSceneRenderer(SceneRendererType::PathTracer);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
	}
	void EditorLayer::UI_Viewport()
	{
		////////////////////////////////////////////////
		//// Viewport
		////////////////////////////////////////////////
		ImGuiWindowFlags viewportWindowFlags = ImGuiWindowFlags_None;
		if (m_IsViewportMaximized)
		{
			const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(mainViewport->Pos);
			ImGui::SetNextWindowSize(mainViewport->Size);
			ImGui::SetNextWindowViewport(mainViewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			viewportWindowFlags |= ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoNavFocus;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin(m_IsViewportMaximized ? "Viewport##Maximized" : "Viewport", nullptr, viewportWindowFlags);
		{
			m_ViewportPanelMouseOver = ImGui::IsWindowHovered();
			m_ViewportPanelFocused = ImGui::IsWindowFocused();

			Application::Get().BlockEvents(!(m_ViewportPanelMouseOver));

			ImVec2 viewportSize = ImGui::GetContentRegionAvail();
			const ImVec2 viewportPosition = ImGui::GetCursorScreenPos();
			m_RendererViewportSize = {
				static_cast<uint32_t>(glm::max(viewportSize.x, 1.0f)),
				static_cast<uint32_t>(glm::max(viewportSize.y, 1.0f))
			};
			if (m_SceneRenderer)
				m_SceneRenderer->SetViewportSize(m_RendererViewportSize.x, m_RendererViewportSize.y);

			m_Camera.SetViewportSize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);

			// Render viewport image
			if (m_SceneRenderer)
				ui::Image(m_SceneRenderer->GetFinalPassImage(), viewportSize, { 0, 1 }, { 1, 0 });
			const bool viewportImageHovered = ImGui::IsItemHovered();
			const bool viewportSelectionRequested = viewportImageHovered &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetDragDropPayload() == nullptr;

			// The payload remains an AssetHandle. Resolve it by metadata here so the
			// Content Browser does not need to know anything about scene editing.
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					const AssetHandle assetHandle = *static_cast<const AssetHandle*>(data->Data);
					const AssetMetadata& metadata = AssetManager::GetMetadata(assetHandle);

					if (metadata.IsValid() && metadata.Type == AssetType::Scene)
					{
						Ref<Scene> scene = AssetManager::GetAsset<Scene>(assetHandle);
						if (scene)
							OpenScene(scene);
					}
					else
					{
						ViewportRay ray;
						if (BuildViewportRay(viewportPosition, viewportSize, ray))
						{
							ViewportHit hit;
							const bool hasSurfaceHit = RaycastScene(ray, hit);
							if (metadata.Type == AssetType::Mesh || metadata.Type == AssetType::MeshSource)
							{
								const AssetHandle meshHandle = ResolveDroppedMesh(assetHandle);
								if (meshHandle)
								{
									const glm::vec3 placement = ComputeDropPlacement(ray, hasSurfaceHit ? &hit : nullptr);
									Ref<Mesh> droppedMesh = AssetManager::GetAsset<Mesh>(meshHandle);
									Ref<MeshSource> droppedMeshSource = droppedMesh ? droppedMesh->GetMeshSource() : nullptr;
									if (droppedMeshSource && droppedMeshSource->GetSubmeshes().size() > 1)
									{
										m_PendingSplitMeshHandle = meshHandle;
										m_PendingSplitPlacement = placement;
										m_PendingSplitParent = {};
										ImGui::OpenPopup("Split Into Submeshes?");
									}
									else
									{
										CreateMeshEntityFromDrop(meshHandle, placement);
									}
								}
							}
							else if (metadata.Type == AssetType::Material && hasSurfaceHit)
							{
								ApplyMaterialFromDrop(assetHandle, hit);
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			RenderSubmeshSplitPopup();

			// ImGuizmo
			if (!m_SelectedEntities.empty() && m_GizmoType != -1)
			{
				ImGuizmo::SetOrthographic(false);
				// ImGuizmo works in absolute ImGui screen coordinates. Match the
				// rendered image exactly; the docked window includes tab/title regions
				// that are not part of the SceneRenderer viewport.
				ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
				ImGuizmo::SetRect(viewportPosition.x, viewportPosition.y, viewportSize.x, viewportSize.y);

				// The gizmo is shown/edited in world space (it's drawn against
				// the camera's view/projection) using the last-selected entity
				// as the pivot -- the "active object" convention other editors
				// use for a multi-selection gizmo, avoiding a separate group
				// centroid to compute and maintain. For a single selection this
				// is just that one entity's world transform.
				Entity pivotEntity = m_SelectedEntities.back();
				// Anchor the widget to the mesh's own AABB center rather than
				// the entity's raw transform origin -- an imported mesh's
				// origin is frequently off to one side (or, for a split
				// submesh entity, wherever that submesh's node happened to
				// sit), which otherwise shows the gizmo floating away from
				// the geometry it's meant to manipulate. Offsetting only the
				// pivot's translation (leaving its rotation/scale basis alone)
				// and still applying deltaMatrix to each entity's own actual
				// world transform below means rotate/scale correctly pivot
				// around that AABB center without changing what gets stored.
				const glm::vec3 pivotLocalOffset = GetEntityGizmoPivotOffset(pivotEntity);
				glm::mat4 pivotWorldTransform = m_Scene->GetWorldSpaceTransform(pivotEntity)
					* glm::translate(glm::mat4(1.0f), pivotLocalOffset);

				// Snap value
				glm::vec3 snap = { 0.f, 0.f, 0.f };
				if (Input::IsKeyPressed(Key::LeftControl))
				{
					if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
						snap = { 45.f, 45.f, 45.f };
					else
						snap = { 0.5f, 0.5f, 0.5f }; // 0.5m for translation and scale
				}

				// deltaMatrix is this frame's incremental WORLD-space transform
				// applied to the pivot -- applying the same delta to every
				// selected entity's own world transform moves/rotates/scales
				// the whole selection together around the pivot.
				glm::mat4 deltaMatrix(1.0f);
				ImGuizmo::Manipulate(glm::value_ptr(m_Camera.GetViewMatrix()),
					glm::value_ptr(m_Camera.GetProjection()),
					(ImGuizmo::OPERATION)m_GizmoType,
					ImGuizmo::LOCAL /* Local space */,
					glm::value_ptr(pivotWorldTransform),
					glm::value_ptr(deltaMatrix),
					glm::value_ptr(snap));

				if (ImGuizmo::IsUsing())
				{
					for (Entity entity : m_SelectedEntities)
					{
						const glm::mat4 newWorldTransform = deltaMatrix * m_Scene->GetWorldSpaceTransform(entity);
						const UUID parentID = entity.GetComponent<RelationshipComponent>().Parent;
						const glm::mat4 parentWorldTransform = parentID
							? m_Scene->GetWorldSpaceTransform(m_Scene->GetEntityByUUID(parentID))
							: glm::mat4(1.0f);
						const glm::mat4 newLocalTransform = glm::inverse(parentWorldTransform) * newWorldTransform;

						auto& tc = entity.GetComponent<TransformComponent>();
						glm::vec3 translation, rotation, scale;
						math::DecomposeTransform(newLocalTransform, translation, rotation, scale);
						tc.SetTranslation(translation);
						tc.SetRotation(glm::degrees(rotation));
						tc.SetScale(scale);
					}
				}
			}

			// Picking stays entirely in the editor. It runs only on a click and does
			// not add an ID attachment, readback or pass to the renderer.
			if (viewportSelectionRequested && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
			{
				ViewportRay ray;
				ViewportHit hit;
				if (BuildViewportRay(viewportPosition, viewportSize, ray) && RaycastScene(ray, hit))
					SelectEntity(hit.HitEntity);
				else
					SelectEntities({});
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();

		if (m_IsViewportMaximized)
			ImGui::PopStyleVar(2);
	}
	void EditorLayer::UI_DockSpace()
	{
		static bool opt_fullscreen_persistant = true;
		static ImGuiDockNodeFlags opt_flags = ImGuiDockNodeFlags_None;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static bool doockSpaceOpen = true;

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();


		io.ConfigWindowsResizeFromEdges = io.BackendFlags & ImGuiBackendFlags_HasMouseCursors;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		// When using ImGuiDockNodeFlags_PassthruDockspace, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
		//if (opt_flags & ImGuiDockNodeFlags_PassthruDockspace)
		//	window_flags |= ImGuiWindowFlags_NoBackground;
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow()->GetNativeWindow());
		bool isMaximized = (bool)glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, isMaximized ? ImVec2(6.0f, 6.0f) : ImVec2(1.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::Begin("DockSpace Demo", &doockSpaceOpen, window_flags);
		ImGui::PopStyleColor(); // MenuBarBg
		ImGui::PopStyleVar(2);

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		UI_RenderTittleBar();

		//ImGui::SetCursorPosY(titlebarHeight + ImGui::GetCurrentWindow()->WindowPadding.y);

		// Dockspace
		float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), opt_flags);
		}

		style.WindowMinSize.x = minWinSizeX;
		ImGui::End();
	}
}
