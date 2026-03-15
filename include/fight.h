#ifndef FIGHT_H
#define FIGHT_H

#include "types.h"

// Fight between two female apes when they encounter
// Uses generic fight mechanics from fight.c
void female_fight(FemaleApe* ape1, FemaleApe* ape2, SimulationState* sim);

// Fight between two male apes over basket contents
// Uses hybrid termination conditions:
// - Energy threshold → withdrawal
// - Damage cap → defeat without withdrawal
// - Basket empty → fight becomes pointless
// - Max rounds → draw
void male_fight(MaleApe* male1, MaleApe* male2, SimulationState* sim);

#endif