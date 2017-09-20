FROM ubuntu:16.04
MAINTAINER John Starich <johnstarich@johnstarich.com>

# Install set up tools
RUN apt-get update && \
    DEBIAN_FRONTEND=noninterative \
        apt-get install -y --no-install-recommends \
            curl \
            tar

# Install useful user programs
RUN apt-get update && \
    DEBIAN_FRONTEND=noninterative \
        apt-get install -y --no-install-recommends \
            coreutils \
            manpages-dev \
            xorg openbox \
            ncurses-dev \
            wget \
            vim emacs \
            gcc clang make \
            gdb ddd \
            qemu

 # Clean up apt-get's files
RUN apt-get clean autoclean && \
    rm -rf /var/lib/apt/* /var/lib/cache/* /var/lib/log/*           

COPY src /pintos
RUN /pintos/env-prep.sh

WORKDIR /pintos

# Add Pintos to PATH
ENV PATH=/pintos/utils:$PATH

CMD ["sleep", "infinity"]
