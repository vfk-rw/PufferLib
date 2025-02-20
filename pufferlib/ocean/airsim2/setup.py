from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy as np
import platform
import os

raylib_path = os.path.abspath("../../../raylib/lib")
os.environ["LD_LIBRARY_PATH"] = f"{raylib_path}:{os.environ.get('LD_LIBRARY_PATH','')}"

compile_args = ["-Wall", "-DPLATFORM_DESKTOP", "-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION"]
link_args = []

if platform.system() == "Linux":
    compile_args.extend(["-g", "-O2"])
else:
    compile_args.append("-O3")

extensions = [
    Extension(
        "cy_airsim2",
        ["cy_airsim2.pyx", "log.c"],  # Added log.c to sources
        include_dirs=[np.get_include(), ".", "../../../raylib/include"],
        library_dirs=[raylib_path],
        libraries=["raylib", "m", "pthread", "yaml"],
        extra_compile_args=compile_args,
        extra_link_args=link_args,
        runtime_library_dirs=[raylib_path],
    )
]

setup(ext_modules=cythonize(extensions))