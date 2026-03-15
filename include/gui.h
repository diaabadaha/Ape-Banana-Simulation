#ifndef GUI_H
#define GUI_H

#include "types.h"

/*
 * Launches an OpenGL (FreeGLUT) window to visualize the simulation in real-time.
 * 
 * Features:
 * - 1600x900 window with maze (80%) and stats panel (20%)
 * - Real-time ape movement with smooth animations
 * - Family basket tracking with bar graphs
 * - Energy bars for all apes (Green→Yellow→Red)
 * - Fight animations with red pulsing glow
 * - Baby stealing animations
 * - Interactive controls (pause, speed adjustment, click to select)
 * - 30 FPS smooth rendering
 * 
 * Controls:
 * - SPACE: Pause/Resume simulation
 * - +/-: Speed control (0.5x, 1x, 2x, 5x, 10x)
 * - Click on ape: Show detailed stats in panel
 * - D: Toggle details view
 * - ESC/Q: Quit and return to menu
 * 
 * This call BLOCKS until the user closes the window.
 * The simulation continues running in background threads.
 */
void gui_show_simulation(SimulationState* sim);

#endif