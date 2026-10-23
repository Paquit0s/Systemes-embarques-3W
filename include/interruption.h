#ifndef INTERRUPTION_H
#define INTERRUPTION_H

#include <stdbool.h>

/**
 * @brief Initialise l'interruption externe (EXTI) du bouton sur PC13.
 */
void Interruption_Init(void);

/**
 * @brief Vérifie de manière atomique si un événement bouton a eu lieu et le consomme.
 * @return true si un appui valide (anti-rebond 200 ms) a été détecté, false sinon.
 */
bool Interruption_ConsumeButtonEvent(void);

#endif /* INTERRUPTION_H */