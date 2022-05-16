import os
import subprocess
import CheckPython

# Make sure everything we need is installed
CheckPython.ValidatePackages()

import Vulkan
import Utils

# Change from Scripts directory to root
os.chdir('../')

if (not Vulkan.CheckVulkanSDK()):
    print("Vulkan SDK not installed.")
    
if (not Vulkan.CheckVulkanSDKDebugLibs()):
    print("Vulkan SDK debug libs not found.")

subprocess.call(["git", "lfs", "pull"])

ide = Utils.ChooseIDE()

print(f"Running premake with target: {ide}")

if (ide == 'vs2019'):
    subprocess.call(["vendor/premake/bin/premake5.exe", ide])
else:
    subprocess.call(["vendor/premake/bin/premake5.exe", ide])