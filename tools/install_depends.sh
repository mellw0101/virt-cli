install_dependencies() 
{
    # Check the distribution
    if [ -x "$(command -v apt)" ]; then
        # Debian/Ubuntu
        sudo apt update
        sudo apt install -y libcurl4-openssl-dev
    elif [ -x "$(command -v dnf)" ]; then
        # Fedora/RHEL
        sudo dnf install -y package1 package2
    elif [ -x "$(command -v pacman)" ]; then
        # Arch Linux
        sudo pacman -Syu --noconfirm                  
    else
        echo "Unsupported distribution. Please install dependencies manually."
        exit 1
    fi
}

install_dependencies