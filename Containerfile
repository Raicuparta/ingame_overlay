# Portable Linux build environment for the overlay.
#
# manylinux2014 is CentOS 7 based, so it ships glibc 2.17. That is old enough
# for the resulting .so to load under the Steam Linux Runtime (scout) as well
# as modern distros, which is the whole point: a binary built on a bleeding
# edge distro pulls in symbol versions (e.g. GLIBC_2.43) that most game
# runtimes don't have.
FROM quay.io/pypa/manylinux2014_x86_64

RUN yum install -y \
        mesa-libGL-devel \
        libX11-devel \
        libstdc++-static \
        pkgconfig \
 && yum clean all

WORKDIR /work
