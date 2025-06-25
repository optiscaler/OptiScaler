#!/bin/bash

clear

echo " ::::::::  :::::::::  ::::::::::: :::::::::::  ::::::::   ::::::::      :::     :::        :::::::::: :::::::::  "
echo ":+:    :+: :+:    :+:     :+:         :+:     :+:    :+: :+:    :+:   :+: :+:   :+:        :+:        :+:    :+: "
echo "#+:    +:+ +:+    +:+     +:+         +:+     +:+        +:+         +:+   +:+  +:+        +:+        +:+    +:+ "
echo "+#+    +:+ +#++:++#+      +#+         +#+     +#++:++#++ +#+        +#++:++#++: +#+        +#++:++#   +#++:++#:  "
echo "+#+    +#+ +#+            +#+         +#+            +#+ +#+        +#+     +#+ +#+        +#+        +#+    +#+ "
echo "#+#    #+# #+#            #+#         #+#     #+#    #+# #+#    #+# #+#     #+# #+#        #+#        #+#    #+# "
echo " ########  ###            ###     ###########  ########   ########  ###     ### ########## ########## ###    ### "
echo ""
echo "Coping is strong with this one..."
echo ""

# Get the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPTISCALER_FILE="$SCRIPT_DIR/OptiScaler.dll"

# Remove junk files
rm -f "$SCRIPT_DIR/!! EXTRACT ALL FILES TO GAME FOLDER !!" 2>/dev/null
rm -f "$SCRIPT_DIR/setup_windows.bat" 2>/dev/null

# Check if OptiScaler.dll exists
if [ ! -f "OptiScaler.dll" ]; then
    echo "OptiScaler \"OptiScaler.dll\" file is not found!"
    echo "Please make sure you extracted all OptiScaler files to the game folder."
    echo ""
    echo "For Unreal Engine games, look for the game executable in:"
    echo "- <path-to-game>/Game-or-Project-name/Binaries/Win64/"
    echo "- Ignore the Engine folder"
    echo ""
    read -p "Press Enter to exit..."
    exit 1
fi

# Unreal Engine detection
if [ -d "$SCRIPT_DIR/Engine" ]; then
    echo "Found Engine folder, if this is an Unreal Engine game then please extract OptiScaler to #CODENAME#/Binaries/Win64"
    echo ""

    while true; do
        read -p "Continue installation to current folder? [y/n]: " continue_choice
        continue_choice=$(echo "$continue_choice" | tr -d ' ')

        if [ "$continue_choice" = "y" ] || [ "$continue_choice" = "Y" ]; then
            break
        elif [ "$continue_choice" = "n" ] || [ "$continue_choice" = "N" ]; then
            echo "Installation cancelled."
            read -p "Press Enter to exit..."
            exit 0
        fi
    done
fi

select_filename() {
    while true; do
        echo ""
        echo "Choose a filename for OptiScaler (default is dxgi.dll):"
        echo " [1] dxgi.dll"
        echo " [2] winmm.dll"
        echo " [3] version.dll"
        echo " [4] dbghelp.dll"
        echo " [5] d3d12.dll"
        echo " [6] wininet.dll"
        echo " [7] winhttp.dll"
        echo " [8] OptiScaler.asi"

        read -p "Enter 1-8 (or press Enter for default): " filename_choice

        case "$filename_choice" in
            ""|"1")
                selected_filename="dxgi.dll"
                ;;
            "2")
                selected_filename="winmm.dll"
                ;;
            "3")
                selected_filename="version.dll"
                ;;
            "4")
                selected_filename="dbghelp.dll"
                ;;
            "5")
                selected_filename="d3d12.dll"
                ;;
            "6")
                selected_filename="wininet.dll"
                ;;
            "7")
                selected_filename="winhttp.dll"
                ;;
            "8")
                selected_filename="OptiScaler.asi"
                ;;
            *)
                clear
                echo "Invalid choice. Please select a valid option."
                echo ""
                continue
                ;;
        esac

        # Check if file already exists
        if [ -f "$selected_filename" ]; then
            echo ""
            echo "WARNING: $selected_filename already exists in the current folder."
            echo ""

            while true; do
                read -p "Do you want to overwrite it? [y/n]: " overwrite_choice
                overwrite_choice=${overwrite_choice,,} 

                if [[ "$overwrite_choice" =~ ^(yes|y)$ ]]; then
                    break 2  # Break out of both loops
                elif [[ "$overwrite_choice" =~ ^(no|n)$ ]]; then
                    clear
                    break  # Break out of the inner loop, continue filename selection
                else
                    clear
                    echo "Invalid choice. Please enter 'y' or 'n'."
                fi
            done
	    else
            break  # File doesn't exist, proceed
        fi
    done
}

select_filename

# Try to detect Nvidia
NVIDIA_DETECTED=false
if command -v nvidia-smi >/dev/null 2>&1; then
    if nvidia-smi >/dev/null 2>&1; then
        NVIDIA_DETECTED=true
        echo "Nvidia GPU detected."
    fi
fi

while true; do
    echo ""
    if [ "$NVIDIA_DETECTED" = true ]; then
        default_value="y"
        read -r -p "Are you using an Nvidia GPU [Y/n]: " using_nvidia
    else
        default_value="n"
        read -r -p "Are you using an Nvidia GPU [y/N]: " using_nvidia
    fi

    using_nvidia=${using_nvidia,,}
    using_nvidia=${using_nvidia:-$default_value}
    
    if [[ "$using_nvidia" =~ ^(no|n)$ ]]; then
        while true; do
            echo ""
            read -r -p "Will you try to use DLSS inputs? (enables spoofing, required for DLSS FG, Reflex->AL2) [Y/n]: " using_dlss

            using_dlss=${using_dlss,,}
            using_dlss=${using_dlss:-y}

            if [[ "$using_dlss" =~ ^(no|n)$ ]]; then
                # Disable spoofing
                config_file="OptiScaler.ini"
                if [ ! -f "$config_file" ]; then
                    echo "Config file not found: $config_file"
                    read -p "Press Enter to continue..."
                else
                    # Use sed to replace Dxgi=auto with Dxgi=false
                    sed -i 's/Dxgi=auto/Dxgi=false/g' "$config_file"
                    echo "Spoofing disabled in configuration."
                fi
                break
            elif [[ "$using_dlss" =~ ^(yes|y)$ ]]; then
                break
            else
                echo "Invalid choice. Please enter 'y' or 'n'."
                continue
            fi
        done
        break
    elif [[ "$using_nvidia" =~ ^(yes|y)$ ]]; then
        break
    else
        echo "Invalid choice. Please enter 'y' or 'n'."
        continue
    fi
done

# Rename OptiScaler file
echo ""
if [ "$overwrite_choice" = "y" ] || [ "$overwrite_choice" = "Y" ]; then
    echo "Removing previous $selected_filename..."
    rm -f "$selected_filename"
fi

echo "Renaming OptiScaler file to $selected_filename..."
if ! mv "$OPTISCALER_FILE" "$selected_filename"; then
    echo ""
    echo "ERROR: Failed to rename OptiScaler file to $selected_filename."
    echo "Please check file permissions and try again."
    read -p "Press Enter to exit..."
    exit 1
fi

# Create uninstaller
create_uninstaller() {
    cat > "remove_optiscaler.sh" << 'EOF'
#!/bin/bash

clear
echo " ::::::::  :::::::::  ::::::::::: :::::::::::  ::::::::   ::::::::      :::     :::        :::::::::: :::::::::  "
echo ":+:    :+: :+:    :+:     :+:         :+:     :+:    :+: :+:    :+:   :+: :+:   :+:        :+:        :+:    :+: "
echo "#+:    +:+ +:+    +:+     +:+         +:+     +:+        +:+         +:+   +:+  +:+        +:+        +:+    +:+ "
echo "+#+    +:+ +#++:++#+      +#+         +#+     +#++:++#++ +#+        +#++:++#++: +#+        +#++:++#   +#++:++#:  "
echo "+#+    +#+ +#+            +#+         +#+            +#+ +#+        +#+     +#+ +#+        +#+        +#+    +#+ "
echo "#+#    #+# #+#            #+#         #+#     #+#    #+# #+#    #+# #+#     #+# #+#        #+#        #+#    #+# "
echo " ########  ###            ###     ###########  ########   ########  ###     ### ########## ########## ###    ### "
echo ""
echo "Coping is strong with this one..."
echo ""

read -p "Do you want to remove OptiScaler? [y/n]: " remove_choice

if [ "$remove_choice" = "y" ] || [ "$remove_choice" = "Y" ]; then
    echo ""
    echo "Removing OptiScaler files..."
    
    # Remove OptiScaler files
    rm -f OptiScaler.log
    rm -f OptiScaler.ini
    rm -f SELECTED_FILENAME_PLACEHOLDER
    
    # Remove directories
    rm -rf D3D12_Optiscaler
    rm -rf DlssOverrides
    rm -rf Licenses
    
    echo ""
    echo "OptiScaler removed!"
    echo ""
    
    # Remove this uninstaller
    rm -f "$0"
else
    echo ""
    echo "Operation cancelled."
    echo ""
fi

read -p "Press Enter to exit..."
EOF

    # Replace the placeholder with the actual selected filename
    sed -i "s/SELECTED_FILENAME_PLACEHOLDER/$selected_filename/g" "remove_optiscaler.sh"
    
    # Make the uninstaller executable
    chmod +x "remove_optiscaler.sh"
    
    echo ""
    echo "Uninstaller created: remove_optiscaler.sh"
    echo ""
}

# Create the uninstaller
create_uninstaller

# Success message
clear
echo " OptiScaler setup completed successfully..."
echo ""
echo "  ___                 "
echo " (_         '        "
echo " /__  /)   /  () (/  "
echo "         _/      /    "
echo ""

# Display Wine DLL override information
echo "IMPORTANT FOR LINUX/WINE USERS:"
echo "You might need to add the renamed DLL to Wine overrides"
echo "Example, if using Steam, add this to launch options:"
echo ""
echo "WINEDLLOVERRIDES=$selected_filename=n,b %COMMAND%"
echo ""
echo "Remember: Insert key opens OptiScaler overlay, Page Up/Down for performance stats"
echo ""

# Cleanup - remove setup script
read -p "Press Enter to exit..."

rm -f "$0"

exit 0
