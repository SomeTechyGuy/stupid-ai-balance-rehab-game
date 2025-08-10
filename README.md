# ai-balance-rehab-game
An AI generated (stupid) game that uses the Wii balance board for controlling a player to get score.

![](gameplay.jpg)

Download:
---------

```git clone https://github.com/SomeTechyGuy/stupid-ai-balance-rehab-game.git```

```cd stupid-ai-balance-rehab-game```

Config:
-------

### Change the MAC address (Line 106) to the one you see when connecting the board to Bluetooth

### At lines 30 and 31 change the resolution to the one of your display

### Change line 42 to the refresh rate you currently have

### Lines 143-145 have the player configurations, change the images and names to what you want.

Audio:
------

### For audio, please add the following files in the same directory as the game: main_intro.wav, transition.wav, connection_main.wav, connection_intro.wav, and main_loop.wav

### Note: The files that start with "main" are used on the main game screen (NOT the connection screen). The files that start with "connection" are used on the connection screen. The file with the word "transition" is used during the transition between the connection and "main game" screens.

Files:
------

### Make sure you have in the same directory a file called "shingom.otf" for the font Shin Go Medium

Compiling:
----------

# SDL2 libraries
sudo apt install -y libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libsdl2-image-dev

# Wii Balance Board support
sudo apt install -y libxwiimote-dev libbluetooth-dev

# Audio system
sudo apt install -y pulseaudio pulseaudio-utils

# Graphics drivers (for Pi 4B)
sudo apt install -y mesa-utils libgl1-mesa-dev

## Installation

To compile the game, run:

**Debian/Ubuntu:**
```bash
sudo apt update
sudo apt install -y build-essential git cmake pkg-config libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libsdl2-image-dev libxwiimote-dev libbluetooth-dev pulseaudio pulseaudio-utils
```

**Fedora/CentOS/RHEL:**
```bash
sudo dnf install -y gcc make cmake pkgconfig SDL2-devel SDL2_ttf-devel SDL2_mixer-devel SDL2_image-devel xwiimote-devel bluez-libs-devel pulseaudio-utils
```

**Arch:**
```bash
sudo pacman -S gcc make cmake pkgconfig sdl2 sdl2_ttf sdl2_mixer sdl2_image xwiimote bluez-utils pulseaudio mesa
```

Then compile:
```bash
gcc -o game game.c $(sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lxwiimote -lbluetooth -lm
```

4. **Set up your Wii Balance Board MAC address:**
   - Find your Balance Board's MAC address: `hcitool scan`
   - Edit `game.c` and change the `WII_BB_MAC_ADDRESS` define
   - Recompile the game

5. **Run the game:**
   ```bash
   ./game
   ```

## Game Assets

Make sure you have the following files in your game directory:
- `beep.mp3` - Sound effect for getting a target in balance hold
- `coin.mp3` - Coin collection sound
- `select.mp3` - Menu selection sound
- `target.wav` - Target sound
- `*.wav` - Background music files
- `*.png` - Image assets (player, coin, etc.)
- `*.jpg` - Character images

## Configuration

### Wii Balance Board Setup

1. **Pair your Balance Board:**
   ```bash
   sudo bluetoothctl
   scan on
   # Press sync button on Balance Board
   pair [MAC_ADDRESS]
   trust [MAC_ADDRESS]
   ```

2. **Set permissions:**
   ```bash
   sudo usermod -a -G input,bluetooth $USER
   sudo chmod 666 /dev/input/event* /dev/hidraw*
   ```

## Troubleshooting

### Common Issues

**Balance Board not detected:**
- Check MAC address in code matches your device
- Verify Bluetooth permissions
- Try re-pairing the device

### Debug Mode

Enable debug output by modifying the `DEBUG_INTERVAL` define in the source code.

## Development

### Building from Source

```bash
gcc -o game game.c $(sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lxwiimote -lbluetooth -lm
```

### Code Structure

- **Game States**: Connection, menu, gameplay, winning
- **Input Handling**: Wii Balance Board weight distribution
- **Graphics**: SDL2 rendering with OpenGL acceleration
- **Audio**: SDL2_mixer for music and sound effects
- **Physics**: Custom balance and collision detection

## Acknowledgments

- Nintendo for the Wii Balance Board hardware
- The xwiimote project for Linux Wii device support
- SDL2 development team
- AI assistance in development (Idk any coding language so I used AI)

**Note**: This game requires actual Nintendo Wii Balance Board hardware and is designed for Linux systems. Performance optimization scripts are included for Raspberry Pi 4B users.
