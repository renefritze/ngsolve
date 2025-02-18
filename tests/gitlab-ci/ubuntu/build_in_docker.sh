set -e

# Run ssh-agent (inside the build environment)
eval $(ssh-agent -s)
# Add the SSH key stored in SSH_PRIVATE_KEY variable to the agent store
ssh-add <(echo "$SSH_PRIVATE_KEY")
mkdir -p ~/.ssh
[[ -f /.dockerenv ]] && echo -e "Host *\n\tStrictHostKeyChecking no\n\n" > ~/.ssh/config


if [ "$IMAGE_NAME" == "debug" ]
then
  export CMAKE_CXX_FLAGS="-Og -Wall -Wno-sign-compare -DDebug"
  export CMAKE_BUILD_TYPE="Debug"
else
  export CMAKE_BUILD_TYPE="Release"
fi

if [ "$IMAGE_NAME" == "avx" ] || [ "$IMAGE_NAME" == "avx512" ]
then
  export USE_NATIVE_ARCH="ON"
else
  export USE_NATIVE_ARCH="OFF"
fi

if [ "$IMAGE_NAME" == "mpi" ]
then
  apt-get update && apt-get -y install libopenmpi-dev openmpi-bin gfortran-7
  export CMAKE_ARGS="$CMAKE_ARGS -DUSE_MPI=ON -DMKL_STATIC=OFF -DMKL_SDL=OFF -DUSE_HYPRE=OFF -DUSE_MUMPS=OFF -DMKL_MULTI_THREADED=OFF -DUSE_GUI=OFF"
fi

cd 
cd src/ngsolve
cd external_dependencies
rm -rf netgen
cd ..
git submodule update --init --recursive
cd
mkdir -p build/ngsolve
cd build/ngsolve
cmake ../../src/ngsolve \
  -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" \
  -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
  -DUSE_NATIVE_ARCH=$USE_NATIVE_ARCH \
  -DUSE_OCC=ON \
  -DUSE_CCACHE=ON \
  -DUSE_MKL=ON \
  -DUSE_UMFPACK=ON \
  -DINSTALL_PROFILES=OFF \
  -DMKL_STATIC=ON \
  -DENABLE_UNIT_TESTS=ON \
  -DNG_INSTALL_DIR_LIB=lib/netgen \
  -DNG_INSTALL_DIR_INCLUDE=include/netgen \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_PACKAGING_INSTALL_PREFIX=/usr \
  $CMAKE_ARGS

make -j12
make install
make package
cd ngsolve

if [ "$IMAGE_NAME" == "latest" ]
then
  ## build and upload docu to server
  apt-get install -y rsync

  echo "build docu"
  make docs
  find ~/src/ngsolve/docs/i-tutorials -name '*.ipynb' -print0 | xargs -0 nbstripout
  cp -r ~/src/ngsolve/docs/i-tutorials docs/html/jupyter-files
  zip -r docs/html/i-tutorials.zip docs/html/jupyter-files
  echo "upload docu"
  rsync -ztrl --del -e ssh \
    --rsync-path="mkdir -p deploy/builds/$CI_PIPELINE_ID/docu/ && rsync" \
    docs/html/* \
    gitlab-runner@vector.asc.tuwien.ac.at:deploy/builds/$CI_PIPELINE_ID/docu/
fi
