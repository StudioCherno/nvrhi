project "NVRHI-Vulkan"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	DefaultTargetParams(true)

	forceincludes { "string" }

	files {
		"include/nvrhi/vulkan.h",
		"src/vulkan/**.h",
		"src/vulkan/**.cpp",

		"rtxmu/src/VkAccelStructManager.cpp",
		"rtxmu/src/VulkanSuballocator.cpp",
		"rtxmu/src/Logger.cpp"
	}

	includedirs {
		"include",

		"rtxmu/include"
	}

project "NVRHI-D3D11"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	DefaultTargetParams(true)

	if os.target() == "windows" then
		files {
			"include/nvrhi/d3d11.h",

			"src/common/dxgi-format.h",
			"src/common/dxgi-format.cpp",

			"src/d3d11/**.h",
			"src/d3d11/**.cpp",
		}
	else
		files { "%{HazelRootDirectory}/Hazel-ScriptCore/Source/Dummy.cpp" }
	end

	includedirs {
		"include",

		"rtxmu/include",
	}

project "NVRHI-D3D12"

filter "not system:windows"
    kind "StaticLib"

    -- Mach-y AR requires a non-empty file list for archive creation
    files { "%{HazelRootDirectory}/Hazel-ScriptCore/Source/Dummy.cpp" }

filter "system:windows"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	DefaultTargetParams(true)

	if os.target() == "windows" then
		files {
			"include/nvrhi/d3d12.h",

			"src/common/dxgi-format.h",
			"src/common/dxgi-format.cpp",
			"src/common/versioning.h",

			"src/d3d12/**.h",
			"src/d3d12/**.cpp",

			"rtxmu/src/D3D12AccelStructManager.cpp",
			"rtxmu/src/D3D12Suballocator.cpp",
			"rtxmu/src/Logger.cpp"
		}
	else
		files { "%{HazelRootDirectory}/Hazel-ScriptCore/Source/Dummy.cpp" }
	end

	includedirs {
		"include",
		"thirdparty/DirectX-Headers/include",

		"rtxmu/include",
	}

project "NVRHI"

filter {}

	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	DefaultTargetParams(true)

	forceincludes { "string" }

	links {
		"NVRHI-Vulkan",
		"NVRHI-D3D11",
		"NVRHI-D3D12"
	}

	includedirs {
		"include",

		"rtxmu/include",
	}

	files {
		"include/nvrhi/nvrhi.h",
		"include/nvrhi/utils.h",

		"include/nvrhi/common/**.h",
		"src/common/aftermath.cpp",
		"src/common/format-info.cpp",
		"src/common/misc.cpp",
		"src/common/state-tracking.cpp",
		"src/common/state-tracking.h",
		"src/common/utils.cpp",
		"src/common/versioning.h",

		"src/validation/**.h",
		"src/validation/**.cpp",

		"tools/nvrhi.natvis"
	}
