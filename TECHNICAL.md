# CitySim Technical Specification

## Architecture Overview

CitySim is a tile-based city simulation game written in C for the DOS platform, specifically targeting 386 processors with EGA graphics capability.

## System Architecture

### Hardware Layer
- **Processor**: Intel 386 or compatible
- **Memory**: Maximum 4MB RAM
- **Graphics**: EGA (Enhanced Graphics Adapter)
  - Resolution: 640x350 pixels
  - Colour depth: 4-bit (16 colours)
  - Video memory: 0xA000 segment
- **Input**: IBM PC/XT keyboard

### Software Layer
- **Operating System**: MS-DOS 5.0 or later
- **DOS Extender**: DOS4GW (Tenberry Software)
- **Compiler**: Open Watcom C/C++ 386
- **Calling Convention**: Register-based (Watcom)

## Memory Layout

### Static Allocation

```
Total Memory Budget: ~4MB
├── Code Segment: ~50KB
│   ├── main.c compiled
│   ├── graphics.c compiled  
│   └── game.c compiled
├── Data Segment: ~140KB
│   ├── GameState structure: ~135KB
│   │   ├── Map data: 64x64x6 bytes = 24,576 bytes
│   │   ├── Human array: 256x36 bytes = 9,216 bytes
│   │   ├── Building array: 512x12 bytes = 6,144 bytes
│   │   └── Game variables: ~500 bytes
│   └── Static data: ~5KB
├── Stack: 64KB
├── DOS4GW overhead: ~500KB
└── Free memory: ~3.2MB
```

### Dynamic Allocation

The game uses minimal dynamic allocation:
- All major structures are statically allocated
- No malloc/free during gameplay
- Fixed-size arrays prevent fragmentation

## Data Structures

### Tile Structure (6 bytes)
```c
typedef struct {
    unsigned char type;        // 1 byte - tile type ID
    unsigned char population;  // 1 byte - 0-255 residents
    unsigned char power;       // 1 byte - boolean flag
    unsigned char water;       // 1 byte - boolean flag
    unsigned char development; // 1 byte - 0-100 progress
    unsigned char pollution;   // 1 byte - 0-100 level
} Tile;
```

### Human Structure (36 bytes)
```c
typedef struct {
    char name[20];            // 20 bytes - citizen name
    unsigned char x, y;       // 2 bytes - current position
    unsigned char age;        // 1 byte - 0-255 years
    unsigned char activity;   // 1 byte - current activity
    unsigned char home_x, home_y;     // 2 bytes - home location
    unsigned char work_x, work_y;     // 2 bytes - work location
    unsigned char happiness;  // 1 byte - 0-100
    unsigned char health;     // 1 byte - 0-100
    unsigned char wealth;     // 1 byte - 0-100
    unsigned char education;  // 1 byte - 0-100
    // Total: 36 bytes, padding for alignment
} Human;
```

### Building Structure (12 bytes)
```c
typedef struct {
    unsigned char type;       // 1 byte - building type
    unsigned char x, y;       // 2 bytes - position
    unsigned char width, height;      // 2 bytes - dimensions
    unsigned char capacity;   // 1 byte - max occupancy
    unsigned char occupancy;  // 1 byte - current occupancy
    unsigned char condition;  // 1 byte - 0-100 health
    // Total: 12 bytes including padding
} Building;
```

### GameState Structure (~135KB)
```c
typedef struct {
    Tile map[64][64];             // 24,576 bytes
    Human humans[256];            // 9,216 bytes
    Building buildings[512];      // 6,144 bytes
    unsigned int population;      // 2 bytes
    unsigned int num_buildings;   // 2 bytes
    unsigned int num_humans;      // 2 bytes
    long funds;                   // 4 bytes
    unsigned int game_time;       // 2 bytes (hours)
    unsigned int game_day;        // 2 bytes
    unsigned char scroll_x, scroll_y;        // 2 bytes
    unsigned char cursor_x, cursor_y;        // 2 bytes
    unsigned char current_tool;   // 1 byte
    unsigned char game_state;     // 1 byte
    unsigned char selected_human; // 1 byte
} GameState;
```

## Graphics System

### EGA Programming

**Mode Initialisation**
```
INT 10h, AH=00h, AL=10h
- Sets 640x350, 16-colour mode
- 4 bit planes
- 80 bytes per scanline
```

**Pixel Writing**
```
1. Calculate offset: (y * 80) + (x / 8)
2. Calculate bit mask: 0x80 >> (x % 8)
3. Set EGA registers:
   - Port 0x3CE, Register 0x08: Bit Mask
   - Port 0x3CE, Register 0x05: Write Mode 2
4. Write colour byte to video memory
5. Reset registers to defaults
```

**Colour Palette (EGA Standard)**
```
0 = Black       4 = Red         8 = Dark Grey      12 = Light Red
1 = Blue        5 = Magenta     9 = Light Blue     13 = Light Magenta  
2 = Green       6 = Brown      10 = Light Green    14 = Yellow
3 = Cyan        7 = Light Grey 11 = Light Cyan     15 = White
```

### Rendering Pipeline

1. **Clear Screen** (single-colour fill)
2. **Draw Map Layer**
   - Iterate visible tiles (80x43 viewport)
   - Draw each 8x8 tile sprite
   - Apply tile colouring based on type
3. **Draw UI Overlay**
   - Status bar (bottom 6 pixels)
   - Text rendering (custom 5x7 font)
4. **Draw Cursor** (white outline)

### Text Rendering

Custom bitmap font:
- Character size: 5x7 pixels
- Spacing: 6 pixels horizontal, 8 pixels vertical
- 96 glyphs (ASCII 32-127)
- Direct pixel plotting

## Game Logic

### Update Cycle

**Main Loop** (50ms per frame = 20 FPS)
```
1. Handle input
2. Update game state
   - Increment time counter
   - Every 18 ticks (≈1 second):
     - Advance game hour
     - Update tile services
     - Update human behaviours
     - Update building states
     - Collect taxes (at midnight)
3. Render current view
4. Delay 50ms
```

### Tile Service Propagation

**Power/Water Distribution**
- Algorithm: Flood fill within 5-tile radius
- Source buildings: Power Plant, Water Pump
- Complexity: O(n²) where n = 11 (diameter)
- Frequency: Once per game hour

**Pseudocode**
```
for each tile in map:
    reset power and water flags
    
for each source building:
    for y in (source_y - 5) to (source_y + 5):
        for x in (source_x - 5) to (source_x + 5):
            set service flag for tile[y][x]
```

### Human Behaviour Simulation

**Activity Scheduler**
```
Based on game hour (0-23):
00-06: SLEEPING (at home)
06-08: EATING (at home)
08-09: TRAVELING (moving to work)
09-17: WORKING (at workplace)
17-18: TRAVELING (moving home)
18-19: EATING (at home)
19-22: LEISURE or SHOPPING (random location)
22-24: SLEEPING (at home)
```

**Movement Algorithm**
```
if activity == TRAVELING:
    if current_x < destination_x:
        current_x += 1
    if current_x > destination_x:
        current_x -= 1
    if current_y < destination_y:
        current_y += 1
    if current_y > destination_y:
        current_y -= 1
```

**Stat Updates** (probabilistic, checked hourly)
```
5% chance per hour:
    if has power AND water:
        happiness += 1 (max 100)
    else:
        happiness -= 1 (min 0)
    
    if pollution > 50:
        health -= 1 (min 0)
    else:
        health += 1 (max 100)
```

### Population Growth

**Spawning Conditions**
```
For each residential tile:
    if has power AND has water:
        0.2% chance per hour:
            spawn new human at this location
```

**Initial Stats** (random ranges)
```
Age: 18-65
Happiness: 50-80
Health: 60-90
Wealth: 30-70
Education: 40-80
```

### Economy

**Income Sources**
- Tax collection: $5 per citizen per 24 hours
- Collection time: Midnight (game hour 0)

**Expenses**
- Building costs (one-time payment)
- No operational costs (simplified model)

## Input System

### Keyboard Handling

**DOS Interrupts**
```
INT 16h, AH=01h - Check for keystroke
INT 16h, AH=00h - Read keystroke
```

**Key Processing**
- Extended keys (arrows): Two-byte sequence (0x00 or 0xE0, then scancode)
- Regular keys: Single byte ASCII
- No keyboard buffer overflow protection needed (DOS handles)

**Input Queue**
- DOS provides built-in keyboard buffer
- Game checks once per frame
- No key repeat required (DOS provides)

## File I/O

### Current Implementation
- No save/load functionality
- All data in-memory only

### Future Implementation Plan
```c
Save file format (binary):
Header (16 bytes):
    char magic[4] = "CTSM"
    unsigned int version = 1
    unsigned int checksum
    unsigned int reserved[2]
    
GameState structure (raw binary dump)
    
Footer (4 bytes):
    unsigned int end_marker = 0xDEADBEEF
```

## Performance Characteristics

### Computational Complexity

**Per Frame**
- Input handling: O(1)
- Rendering: O(w × h) where w=80, h=43 tiles = 3,440 operations
- UI drawing: O(1)

**Per Game Hour**
- Service propagation: O(m × n × s) where m=64, n=64, s=sources ≈ 4,096 × s
- Human updates: O(p) where p=population ≤ 256
- Building updates: O(b) where b=buildings ≤ 512

**Bottlenecks**
1. Pixel drawing (direct video memory writes)
2. Service propagation (quadratic on map size)
3. Text rendering (no hardware acceleration)

### Optimisation Techniques

**Graphics**
- Write mode 2 (colour expansion in hardware)
- Minimal overdraw (only changed regions)
- Simplified tile sprites (solid colours)

**Game Logic**
- Probabilistic updates (not every entity every frame)
- Lazy evaluation (only visible tiles in detail)
- Fixed-point arithmetic (no FPU required)

**Memory**
- Structure padding aligned to 4-byte boundaries
- Sequential memory access patterns (cache-friendly)
- No dynamic allocation (no fragmentation)

## Limitations and Trade-offs

### Design Constraints

1. **Fixed Map Size**: 64×64 tiles
   - Reason: Memory budget
   - Impact: Limited city extent

2. **Population Cap**: 256 humans
   - Reason: Memory + update performance
   - Impact: Simplified demographics

3. **Building Limit**: 512 structures
   - Reason: Memory allocation
   - Impact: Density restrictions

4. **No Pathfinding**: Direct line movement
   - Reason: CPU performance
   - Impact: Unrealistic traffic

5. **Simple Economics**: No complex budgeting
   - Reason: Gameplay simplicity
   - Impact: Limited strategy depth

### Future Enhancements

**Memory Permitting**
- Increase population to 512
- Expand map to 128×128
- Add more building types

**CPU Permitting**
- A* pathfinding for humans
- Traffic simulation
- Pollution propagation
- Crime mechanics

**Storage Permitting**
- Save/load system
- Multiple city slots
- Campaign mode

## Compatibility

### Tested Platforms
- DOSBox 0.74 (Linux, Windows, macOS)
- 86Box with 386 emulation
- Real 386 hardware (limited testing)

### Known Issues
- No Windows NT/2000/XP DOS box support (EGA access restricted)
- Requires DOS4GW (not bundled)
- No sound card support
- No mouse support

### Future Compatibility
- Consider SVGA support (640×480, 256 colours)
- Add DOS32A as alternative extender
- Port to DJGPP for broader compatibility

## Development Environment

### Build Requirements
- Open Watcom C/C++ 1.9 or later
- DOS4GW DOS extender
- Make utility (wmake)

### Recommended Tools
- DOSBox for testing
- Hex editor for debugging
- Resource editor for future assets

### Source Code Statistics
```
citysim.h:   ~200 lines (headers, structures)
graphics.c:  ~400 lines (rendering)
game.c:      ~450 lines (logic, simulation)
main.c:      ~50 lines (game loop)
Total:       ~1,100 lines of C code
```

## References

### Technical Documentation
- Intel 80386 Programmer's Reference Manual
- IBM EGA Technical Reference
- Open Watcom C/C++ User's Guide
- DOS Protected Mode Interface (DPMI) Specification

### Programming Resources
- Michael Abrash's Graphics Programming Black Book
- Ralf Brown's Interrupt List
- DOS4GW Technical Reference

---

Document Version: 1.0
Last Updated: February 2026
