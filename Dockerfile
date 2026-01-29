FROM archlinux:latest

# Update system and install base dependencies
RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
        base-devel \
        git \
        python \
        curl \
        sudo \
        # AUR package build dependencies
        gmp \
        libmpc \
        mpfr \
        readline \
        zlib \
        autoconf \
        automake \
        bison \
        flex \
        gettext \
        gperf \
        ncurses \
        rsync \
        texinfo \
        wget

# Create build user (makepkg cannot run as root)
RUN useradd -m builder && \
    echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Switch to builder user for AUR package installation
USER builder
WORKDIR /home/builder

# Clone and build the m68k-amigaos-gcc AUR package
# Modify PKGBUILD to show verbose output (remove output redirections)
RUN git clone https://aur.archlinux.org/m68k-amigaos-gcc.git && \
    cd m68k-amigaos-gcc && \
    sed -i 's/> *\/dev\/null//g; s/2>&1//g' PKGBUILD && \
    makepkg -si --noconfirm

# Install lha from AUR (use gnu89 for K&R C compatibility)
RUN cd /home/builder && \
    git clone https://aur.archlinux.org/lha.git && \
    cd lha && \
    CFLAGS="-std=gnu89 -Wno-error -O2" makepkg -si --noconfirm

# Clean up build files to reduce image size
RUN rm -rf /home/builder/m68k-amigaos-gcc /home/builder/lha

# Switch back to root for final setup
USER root

# Set up working directory for builds
WORKDIR /build

# Default command
CMD ["/bin/bash"]
