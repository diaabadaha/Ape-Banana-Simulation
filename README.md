# 🐒 Ape Banana Simulation
> Real-Time Applications & Embedded Systems | ENCS4330 | Birzeit University

A Linux-based multi-threaded simulation of ape families collecting bananas in a 2D maze. Each ape — female, male, and baby — runs as its own **POSIX thread**, interacting through shared state protected by **mutexes** and **condition variables**. The simulation is visualized in real time using **OpenGL (GLUT)**.

---

## 📁 Project Structure

```
.
├── src/
│   ├── main.c                  # Entry point & simulation startup
│   ├── config.c                # Config file parsing with defaults
│   ├── maze.c                  # 2D maze generation & banana distribution
│   ├── sim_state.c             # Global simulation state management
│   ├── simulation_threading.c  # Thread creation & lifecycle management
│   ├── female_ape.c            # Female ape thread logic (collect & return)
│   ├── male_ape.c              # Male ape thread logic (guard & fight)
│   ├── baby_ape.c              # Baby ape thread logic (steal & eat)
│   ├── fight.c                 # Fight resolution between apes
│   ├── sync.c                  # Synchronization primitives & helpers
│   ├── graphics.c              # OpenGL rendering primitives
│   ├── gui.c                   # Full GUI rendering & animation
│   ├── menu_parser.c           # Parses menu config file
│   ├── menu_handler.c          # Menu interaction handling
│   └── utils.c                 # General utility functions
├── include/
│   ├── types.h                 # All structs & enums (Family, FemaleApe, MaleApe, BabyApe)
│   ├── config.h                # Config struct & parameter definitions
│   ├── maze.h                  # Maze struct & cell definitions
│   ├── sync.h                  # Mutex/condition variable declarations
│   ├── simulation_threading.h  # Thread management declarations
│   ├── female_ape.h
│   ├── male_ape.h
│   ├── baby_ape.h
│   ├── fight.h
│   ├── graphics.h
│   ├── gui.h
│   ├── sim_state.h
│   ├── menu_handler.h
│   ├── menu_parser.h
│   └── utils.h
├── config/
│   ├── arguments.txt           # All simulation parameters (user-defined)
│   ├── maze.txt                # Pre-defined maze layout (optional)
│   └── menu.txt                # Menu display configuration
├── results/                    # Simulation output logs
└── Makefile
```

---

## 🐒 Simulation Overview

The simulation models a full ape family ecosystem inside a 2D maze:

### Female Apes
- Each female ape runs as a **POSIX thread** and enters the maze to collect bananas
- She navigates the maze using a sensing radius to find banana-rich cells
- Once she collects a user-defined number of bananas, she exits and deposits them in the family basket
- If she encounters another female ape while returning, a **fight may occur** over who keeps the bananas
- Female apes lose energy while moving and fighting; when energy drops below a threshold, they **rest** (using condition variables) before continuing

### Male Apes
- Each male ape runs as a **POSIX thread** and guards the family basket
- Fights can erupt between **neighboring male apes** over basket contents — the probability of a fight increases as the basket fills up
- The winner takes all the bananas from the loser's basket
- Male apes lose energy from guarding and fighting; when energy is critically low, the **entire family withdraws** from the simulation

### Baby Apes
- Each baby ape runs as a **POSIX thread** and opportunistically steals bananas during male ape fights
- Stolen bananas are either added to the dad's basket or **eaten** by the baby
- Baby apes track stolen, eaten, and caught counts throughout the simulation

### Families
Each family is a self-contained unit consisting of one male, one female, and a configurable number of babies. All family members share access to the family basket, protected by synchronization primitives.

---

## 🔚 Termination Conditions

The simulation ends when **any** of the following is true:

| Condition | Default Threshold |
|-----------|-----------------|
| Number of withdrawn families exceeds threshold | 2 families |
| A single family collects more than N bananas | 200 bananas |
| A baby ape eats more than N bananas | 50 bananas |
| Simulation running time exceeds time limit | 300 seconds |

All thresholds are configurable in `config/arguments.txt`.

---

## 🔀 Threading & Synchronization

The simulation uses a **pure multi-threading approach** with POSIX threads:

- Every ape (female, male, baby) is a separate `pthread`
- Shared resources (basket, maze cells, simulation state) are protected with **mutexes**
- Female ape resting uses **condition variables** (`pthread_cond_wait`) to avoid busy-waiting
- Fight sequences use **fine-grained locking** to prevent deadlocks between competing apes
- Simulation termination is coordinated through a shared flag checked by all threads

---

## ⚙️ Configuration (`config/arguments.txt`)

All parameters are user-defined — no recompilation needed:

```ini
# Maze
MAZE_SIZE = 20
OBSTACLE_DENSITY = 0.4
BANANA_DENSITY = 0.5
MAX_BANANAS_PER_CELL = 5

# Families
NUM_FAMILIES = 7
BABY_COUNT_PER_FAMILY = 3
BANANAS_PER_TRIP = 10

# Energy
FEMALE_ENERGY_MAX = 100
MALE_ENERGY_MAX = 120
FEMALE_ENERGY_THRESHOLD = 35
MALE_ENERGY_THRESHOLD = 25
ENERGY_LOSS_MOVE = 1
ENERGY_LOSS_FIGHT = 15
ENERGY_GAIN_REST = 10

# Fights
FIGHT_PROBABILITY_BASE = 0.3
FIGHT_BASE_DAMAGE = 5
MAX_FIGHT_ROUNDS = 30

# Baby Stealing
BABY_STEAL_MIN = 1
BABY_STEAL_MAX = 10
BABY_EAT_PROBABILITY = 0.3

# Termination
WITHDRAWN_FAMILY_THRESHOLD = 2
FAMILY_BANANA_THRESHOLD = 200
BABY_EATEN_THRESHOLD = 50
TIME_LIMIT_SECONDS = 300
```

If the config file is missing, hardcoded default values are used automatically.

---

## ⚙️ Requirements

- GCC with POSIX threads support (`-pthread`)
- OpenGL + GLUT for the GUI
- A Linux / Unix environment:
  - **WSL** (Windows Subsystem for Linux) with VS Code
  - Native Linux
  - macOS with GCC via Homebrew

---

## 🔨 Build & Run

### 1. Clone the repository

```bash
git clone https://github.com/diaabadaha/Ape-Banana-Simulation.git
cd Ape-Banana-Simulation
```

### 2. Install dependencies

On Ubuntu / WSL:
```bash
sudo apt update
sudo apt install build-essential freeglut3-dev libglu1-mesa-dev mesa-common-dev
```

Or use the built-in Makefile target:
```bash
make install-deps
```

### 3. Build

```bash
make
```

Other build options:
```bash
make debug    # Build with debug symbols and DEBUG flag
make rebuild  # Clean and rebuild from scratch
```

### 4. Run

```bash
# Run with default config
./bin/ape_simulation

# Run with a custom config file
./bin/ape_simulation config/arguments.txt
```

### 5. Clean

```bash
make clean
```

---

## 💻 Running on WSL with VS Code

1. Open VS Code → press `Ctrl+Shift+P` → search **"WSL: Open Folder in WSL"**
2. Navigate to the cloned project folder
3. Open the integrated terminal with `Ctrl+` `` ` ``
4. Run the build and run commands above directly in the terminal

> Make sure the **WSL** and **C/C++** extensions are installed in VS Code.
> For GUI rendering on WSL, make sure you have an X server running (e.g., VcXsrv or WSLg on Windows 11).

---

## 📚 Course Info

- **Course:** ENCS4330 — Real-Time Applications & Embedded Systems
- **Institution:** Birzeit University
- **Instructor:** Dr. Hanna Bullata
- **Semester:** 1st Semester 2025/2026
