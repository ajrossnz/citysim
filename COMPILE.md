# CitySim - Compilation and Deployment Guide

## Building on Linux with Open Watcom

### Installing Open Watcom

#### Option 1: Download from Official Site
1. Download Open Watcom V2 from: https://github.com/open-watcom/open-watcom-v2/releases
2. Extract to `/opt/watcom` or your preferred location
3. Add to your PATH:
   ```bash
   export WATCOM=/opt/watcom
   export PATH=$WATCOM/binl64:$WATCOM/binl:$PATH
   export INCLUDE=$WATCOM/h
   ```

#### Option 2: Package Manager (if available)
```bash
# Some distributions may have it
sudo apt-get install open-watcom-c-linux   # Debian/Ubuntu (if available)
```

### Compiling the Game

#### Using the Build Script (Recommended)
```bash
chmod +x build.sh
./build.sh
```

#### Using Make Directly
```bash
wmake
```

#### Manual Compilation (if needed)
```bash
wcc386 -bt=dos -6r -fp6 -ox -s -w4 -e25 main.c
wcc386 -bt=dos -6r -fp6 -ox -s -w4 -e25 graphics.c
wcc386 -bt=dos -6r -fp6 -ox -s -w4 -e25 game.c
wlink system dos4g name citysim file {main.obj graphics.obj game.obj}
```

### Compiler Flags Explained

- `-bt=dos` : Build target is DOS
- `-6r` : Generate 386 instructions with register-based calling
- `-fp6` : Generate 387 floating-point code (inline)
- `-ox` : Maximum optimisation
- `-s` : Disable stack checking for speed
- `-w4` : Warning level 4 (high)
- `-e25` : Set error limit to 25

### Expected Output

After successful compilation, you should have:
- `citysim.exe` - The main executable
- `*.obj` - Object files (can be deleted)
- `*.err` - Error files (if any compilation errors occurred)

The game requires DOS4GW.EXE (DOS extender) which comes with Open Watcom.

## Testing in DOSBox

### Installing DOSBox
```bash
# Linux
sudo apt-get install dosbox      # Debian/Ubuntu
sudo dnf install dosbox           # Fedora
sudo pacman -S dosbox             # Arch

# macOS
brew install dosbox
```

### Running the Game
```bash
dosbox citysim.exe
```

### DOSBox Configuration for Optimal Experience

Create or edit `dosbox.conf`:

```ini
[cpu]
core=auto
cputype=386
cycles=10000

[render]
aspect=true
scaler=normal2x

[dos]
memsize=4
```

### DOSBox Controls
- ALT+ENTER: Toggle fullscreen
- CTRL+F10: Release mouse capture
- CTRL+F12: Speed up
- CTRL+F11: Slow down

## Deploying to Real DOS Hardware

### Files to Copy

You need to transfer these files to your DOS machine:
1. `citysim.exe` - The game executable
2. `DOS4GW.EXE` - DOS extender (from Open Watcom installation)

### Location of DOS4GW.EXE

DOS4GW.EXE is located in your Open Watcom installation:
```
$WATCOM/binw/dos4gw.exe
```

### Transfer Methods

#### Via Floppy Disk
```bash
# Mount floppy and copy files
mcopy -i /dev/fd0 citysim.exe ::
mcopy -i /dev/fd0 dos4gw.exe ::
```

#### Via Serial/Parallel Transfer
Use tools like:
- LapLink
- Interlnk/Intersvr (included with DOS 6.0+)
- Kermit

#### Via Network
If your DOS machine has network capability:
- Set up FTP server on Linux
- Use mTCP or other DOS TCP/IP stack to download

### Running on DOS

1. Boot your DOS machine
2. Navigate to the directory containing the files
3. Type: `citysim.exe`
4. Press ENTER

### DOS Memory Configuration

For optimal performance, ensure your CONFIG.SYS includes:

```
DOS=HIGH,UMB
DEVICE=C:\DOS\HIMEM.SYS
DEVICE=C:\DOS\EMM386.EXE NOEMS
FILES=30
BUFFERS=20
```

### Hardware Requirements Check

Before running, verify your system has:
- 386SX or better processor
- 4MB RAM minimum
- EGA graphics card or better
- DOS 5.0 or later

## Troubleshooting

### Compilation Issues

**Error: wcc386 not found**
- Ensure Open Watcom is installed
- Check PATH environment variable
- Verify WATCOM environment variable is set

**Linker errors**
- Ensure all .obj files are present
- Check that DOS4GW is available in $WATCOM/binw/

**Warning about stack size**
- This is normal; the default stack is sufficient

### Runtime Issues in DOSBox

**Game runs too fast**
- Press CTRL+F11 to reduce CPU cycles
- Edit dosbox.conf and set cycles=5000 or lower

**Game runs too slow**
- Press CTRL+F12 to increase CPU cycles
- Edit dosbox.conf and set cycles=max

**Graphics don't display correctly**
- Ensure DOSBox is using correct CPU type (386)
- Check that machine=svga_et4000 in dosbox.conf

### Runtime Issues on Real DOS

**"Packed file is corrupt"**
- DOS4GW.EXE is missing or corrupt
- Retransfer DOS4GW.EXE from Open Watcom

**"Not enough memory"**
- Ensure you have at least 4MB RAM
- Remove TSRs and memory-resident programs
- Use MEMMAKER to optimise memory

**"Runtime error R6009 - not enough space for environment"**
- Increase environment space in CONFIG.SYS:
  `SHELL=C:\DOS\COMMAND.COM /E:1024 /P`

**Graphics corruption**
- Verify EGA/VGA card is functioning
- Try running other EGA programs to test card
- Check monitor cable connections

## Development Notes

### Modifying the Game

To change game parameters, edit `citysim.h`:

```c
#define MAP_WIDTH 64           // Increase for larger maps
#define MAP_HEIGHT 64
#define MAX_HUMANS 256         // Increase for more population
#define TICKS_PER_HOUR 18      // Change game speed
```

Remember to recompile after any changes.

### Memory Constraints

The game is designed for 4MB systems. If you increase MAX_HUMANS or MAX_BUILDINGS significantly, ensure total memory usage stays under:
- 3.5MB for game data
- 500KB for stack and DOS4GW overhead

### Adding Features

The codebase is structured for easy expansion:
- New tile types: Add to tile type enum in citysim.h
- New human activities: Add to activity enum
- New graphics: Modify draw_tile() in graphics.c
- New game mechanics: Add to update_game() in game.c

## Performance Optimisation

### For Slower Systems

1. Reduce MAX_HUMANS to 128
2. Decrease tile update frequency
3. Simplify graphics rendering
4. Reduce tile services calculation range

### For Faster Systems

1. Increase game speed (reduce delay in main.c)
2. Add more detailed graphics
3. Expand simulation complexity
4. Add sound effects

## Additional Resources

- Open Watcom Documentation: http://www.openwatcom.org/
- DOS Programming: http://www.delorie.com/djgpp/doc/
- EGA Programming: "Programmer's Guide to PC & PS/2 Video Systems"

## Support

For issues with:
- Compilation: Check Open Watcom forums
- DOSBox: Visit dosbox.com forums  
- Game bugs: Check the source code comments

Happy city building!
