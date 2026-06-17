#pragma once
#include <stack>
#include <new>
#include <utility>
#include "Core/Handle.h"
namespace cerberus
{
	// TODO: either move this to somewhere else or created our onw stack class
	template<typename T, typename Container = std::deque<T>>
	class Stack : public std::stack<T, Container>
	{
		using std::stack<T, Container>::c;
	public:

		// expose just the iterators of the underlying container
		auto begin() { return std::begin(c); }
		auto end() { return std::end(c); }

		auto begin() const { return std::begin(c); }
		auto end() const { return std::end(c); }

		bool HasElement(const T& element) const
		{
			return std::find(begin(), end(), element) != end();
		}

	};

	template<typename T>
	class Pool
	{
	public:
		Pool()
		{
			m_Data = (T*)::operator new (2 * sizeof(T));
			m_Generations = new uint16_t[2];
			ReAlloc(2);
		}
		~Pool()
		{
			Clear();

			::operator delete(m_Data, m_Capacity * sizeof(T));
			delete[] m_Generations;
		}

		void Clear()
		{
			for (size_t i = 0; i < m_Size; i++)
			{
				const int16_t index = static_cast<int16_t>(i);

				if (!m_FreeIndexes.HasElement(index))
				{
					m_Data[i].~T();

					// Invalidate stale handles.
					m_Generations[i] = UUID_16();

					m_FreeIndexes.push(index);
				}
			}

			m_Size = 0;
		}

		template<typename...Args>
		Handle<T> Emplace(Args&& ... args)
		{
			if (m_FreeIndexes.size() > 0)
			{
				int16_t index = m_FreeIndexes.top();
				UUID_16 generationID = UUID_16();
				m_Generations[index] = generationID;

				// Placement new operator to construct the object inside this specific piece of memory
				new(&m_Data[index]) T(std::forward<Args>(args)...);
				m_FreeIndexes.pop();

				return Handle<T>(generationID, index);

			}

			if (m_Size == m_Capacity)
			{
				size_t newCapacity = m_Capacity + m_Size / 2;
				ReAlloc(newCapacity);
			}

			int16_t index = m_Size;
			UUID_16 generationID = UUID_16();
			m_Generations[index] = generationID;
			// Placement new operator to construct the object inside this specific piece of memory
			new(&m_Data[index]) T(std::forward<Args>(args)...);
			m_Size++;
			return  Handle<T>(generationID, index);
		}

		T* Get(const Handle<T> handle)
		{
			if (!handle.IsValid())
				return nullptr;

			if (handle.m_Index >= m_Capacity)
				return nullptr;

			if (m_FreeIndexes.HasElement(handle.m_Index))
				return nullptr;

			if (m_Generations[handle.m_Index] != handle.m_ID)
				return nullptr;

			return &m_Data[handle.m_Index];
		}

		void Remove(const Handle<T> handle)
		{
			if (!handle.IsValid())
				return;

			if (handle.m_Index >= m_Capacity)
				return;

			if (m_Generations[handle.m_Index] != handle.m_ID)
				return;

			if (m_FreeIndexes.HasElement(handle.m_Index))
				return;

			m_Data[handle.m_Index].~T();

			// Invalidate stale handles.
			m_Generations[handle.m_Index] = UUID_16();

			m_FreeIndexes.push(handle.m_Index);
		}

		/**/
	private:
		void ReAlloc(size_t newCapacity)
		{
			T* newData = (T*)::operator new (newCapacity * sizeof(T));
			uint16_t* newGenerations = new uint16_t[newCapacity];
			for (size_t i = 0; i < newCapacity; i++)
				newGenerations[i] = UUID_16();

			if (newCapacity < m_Size)
			{
				m_Size = newCapacity;
			}

			for (size_t i = 0; i < m_Size; i++)
			{
				if (!m_FreeIndexes.HasElement(i))
				{
					newGenerations[i] = m_Generations[i];
					new (&newData[i]) T(std::move(m_Data[i]));
					m_Data[i].~T();
				}
			}
			::operator delete(m_Data, m_Capacity * sizeof(T));
			delete [] m_Generations;
			 
			m_Capacity = newCapacity;
			m_Data = newData;
			m_Generations = newGenerations;
		}


	private:
		T* m_Data;
		uint16_t* m_Generations;
		
		
		Stack<int16_t> m_FreeIndexes;
		size_t m_Capacity = 0, m_Size = 0;

	};
}