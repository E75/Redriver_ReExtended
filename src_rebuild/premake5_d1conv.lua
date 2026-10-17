-- Driver 1 cities converter: developer CLI around the d1dlc library
project "D1Conv"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++11"
    targetdir "bin/%{cfg.buildcfg}"

    files {
        "d1dlc/**.h",
        "d1dlc/**.cpp",
        "tools/d1conv/**.cpp",
    }

    includedirs {
        "d1dlc",
    }

    filter "system:Windows"
        defines { "_WINDOWS", "_CRT_SECURE_NO_WARNINGS" }

    filter "configurations:Release"
        optimize "Speed"

    filter "configurations:Release_dev"
        optimize "Speed"
        symbols "On"
