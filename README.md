
## Build & run

1. Clone with submodules: `git clone --recursive git://github.com/srtp-team/4dface.git`, or, if you've already cloned it, get the submodules with `git submodule update --init --recursive` inside the `4dface` directory.

2. Make sure you've got boost (>=1.54.0 should do), OpenCV (>=3.0), Eigen (>=3.3.0) and a recent compiler (>=gcc-5, >=clang-4, >=VS2017) installed. For Ubuntu 14.04 and newer, this will do the trick:
    ```
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    sudo apt-get update
    sudo apt-get install gcc-7 g++-7 libboost-all-dev libeigen3-dev libopencv-dev opencv-data
    ```
    For Windows, we recommend [vcpkg](https://github.com/Microsoft/vcpkg/) to install the Boost, OpenCV and Eigen dependencies.

3. Build the app:
    Run from _outside_ the source directory:
    1. `mkdir build && cd build`

    2. `cmake -DCMAKE_INSTALL_PREFIX=../install -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-7 -DCMAKE_CXX_COMPILER=g++-7 -DOpenCV_haarcascades_DIR=/usr/share/opencv/haarcascades/ ../4dface/`

    On Windows, add `-G "Visual Studio 15 2017 Win64"`. Also, you will probably need to add `-C ../4dface/initial_cache.cmake` as first argument - copy the file from `initial_cache.cmake.template` and adjust the paths. Or, with vcpkg, use [`-DCMAKE_TOOLCHAIN_FILE=...`](https://github.com/Microsoft/vcpkg/blob/master/docs/users/integration.md#cmake-toolchain-file-recommended-for-open-source-cmake-projects).

    If you get an error about OpenCV\_haarcascades\_DIR, adjust `-DOpenCV_haarcascades_DIR` to point to the directory of `haarcascade_frontalface_alt2.xml` from OpenCV.

4. Type `make` or build in Visual Studio.

5. Type `make install`, or run the INSTALL target in Visual Studio, to copy all required files into a `share/` directory next to the executable.

6.Move the file `movie_closeeye_isomap.isomap.png` `movie_openeye_isomap.isomap.png` and `Obama.mp4` to the path `4dface\build\Release`.

7.Download file `face_landmarks_model_rcr_68.bin` from `https://pan.baidu.com/s/1qP4nR5Isw5_rxLmVEQu88g`.The key is`zoyy`.And put it in `\4dface\build` 

Then just double-click the `4dface` app from the install-directory or run with `4dface -i videofile` to run on a video.
example:  `cd   \4dface\build\Release`    
`4dface.exe -i Obama.mp4`


## Working with the libraries

If you're interested in working with the libraries, we recommend to clone and build them separately. They come with their own CMake project files and have their own GitHub issues pages.

* [eos](https://github.com/patrikhuber/eos): A lightweight header-only 3D Morphable Face Model fitting library in modern C++11/14
* [superviseddescent](https://github.com/patrikhuber/superviseddescent): A C++11 implementation of the supervised descent optimisation method

## License & contributions

This code is licensed under the Apache License, Version 2.0. The subprojects are also licensed under the Apache License, Version 2.0, except for the 3D morphable face model, which is free for use for non-commercial purposes - for commercial purposes, contact the [Centre for Vision, Speech and Signal Processing](http://www.surrey.ac.uk/cvssp/).

Contributions are very welcome! (best in the form of pull requests.) Please use Github issues for any bug reports, ideas, and discussions.

If you use this code in your own work, please cite one (or both) of the following papers:

* _Fitting 3D Morphable Models using Local Features_, P. Huber, Z. Feng, W. Christmas, J. Kittler, M. Rätsch, IEEE International Conference on Image Processing (ICIP) 2015, Québec City, Canada [[PDF]](http://arxiv.org/abs/1503.02330).

* _A Multiresolution 3D Morphable Face Model and Fitting Framework_, P. Huber, G. Hu, R. Tena, P. Mortazavian, W. Koppen, W. Christmas, M. Rätsch, J. Kittler, International Conference on Computer Vision Theory and Applications (VISAPP) 2016, Rome, Italy [[PDF]](http://www.patrikhuber.ch/files/3DMM_Framework_VISAPP_2016.pdf).
