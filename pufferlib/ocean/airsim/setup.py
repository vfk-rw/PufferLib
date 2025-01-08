from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy as np
import platform
import os

# Set LD_LIBRARY_PATH
raylib_path = os.path.abspath("../../../raylib/lib")
os.environ["LD_LIBRARY_PATH"] = f"{raylib_path}:{os.environ.get('LD_LIBRARY_PATH', '')}"

compile_args = ["-Wall", "-DPLATFORM_DESKTOP", "-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION"]
if platform.system() == "Linux":
    compile_args.extend([
        "-g", "-O0",
        "-fsanitize=address,undefined,bounds,pointer-overflow,leak"
    ])
else:
    compile_args.append("-O3")

extensions = [
    Extension(
        "cy_airsim",
        ["cy_airsim.pyx"],
        include_dirs=[np.get_include(), ".", "../../../raylib/include"],
        library_dirs=[raylib_path],  # Point to directory only
        libraries=["raylib", "m", "pthread"],  # Add raylib to libraries
        extra_compile_args=compile_args,
        runtime_library_dirs=[raylib_path],  # Add runtime path
    )
]

setup(ext_modules=cythonize(extensions))
