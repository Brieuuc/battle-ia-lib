#include "battle_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

// Dimensions et affichage (permet de définir l'échelle de notre "carte" ASCII)
#define GRID_WIDTH 50
#define GRID_HEIGHT 20
#define SCALE_FACTOR 10

// Je n'affiche pas la carte à chaque itération pour ne pas saturer la console.
// On va donc afficher seulement toutes les X itérations
#define DISPLAY_EVERY 5

// Paramètres de comportement :
// (on peut ajuster ces valeurs pour rendre le bot plus ou moins "agressif" ou "peureux")
#define BASE_SPEED 1.0
#define OBSTACLE_PUSH_FACTOR 5.0  // Plus c'est grand, plus on fuit les murs
#define BONUS_PULL_FACTOR 3.0     // Plus c'est grand, plus on fonce vers les bonus
#define ENEMY_TENT_FACTOR 0.5     // Petite attirance vers les ennemis pour mieux les viser
#define OBSTACLE_RADIUS 5.0       // Distance où on commence à être "repoussé" par un obstacle
#define BONUS_RADIUS 50.0         // Distance où on commence à être "attiré" par un bonus
#define MAX_SHOOT_RANGE 10.0      // Portée de tir
#define CRITICAL_HEALTH 50        // Seuil de points de vie jugé "critique"

// On règle le temps de pause (en microsecondes) entre chaque cycle
#define REFRESH_DELAY 300000

// stopThreads : variable globale qui permet de stopper proprement nos threads
static volatile int stopThreads = 0;

// ---------------------------------------------------------
// Fonctions utilitaires
// ---------------------------------------------------------

// distanceBetween : calcule la distance (en 2D) entre deux points
double distanceBetween(BC_Vector3 posA, BC_Vector3 posB) {
    double dx = posB.x - posA.x;
    double dy = posB.y - posA.y;
    return sqrt(dx * dx + dy * dy);
}

// angleBetween : retourne l'angle (en radians) pour aller d'une position "from" à une autre "to"
double angleBetween(BC_Vector3 from, BC_Vector3 to) {
    double dx = to.x - from.x;
    double dy = to.y - from.y;
    return atan2(dy, dx);
}

// ---------------------------------------------------------
// displayMap : affiche la "carte" de manière très simplifiée
// ---------------------------------------------------------
void displayMap(BC_Connection *conn) {
    // Grille de caractères représentant la "carte"
    char grid[GRID_HEIGHT][GRID_WIDTH];

    // On initialise la grille avec des points
    for (int i = 0; i < GRID_HEIGHT; i++) {
        for (int j = 0; j < GRID_WIDTH; j++) {
            grid[i][j] = '.';
        }
    }

    // Récupérer la position du robot (mon bot)
    BC_PlayerData me = bc_get_player_data(conn);
    int posX = (int)(me.position.x / SCALE_FACTOR) % GRID_WIDTH;
    int posY = (int)(me.position.y / SCALE_FACTOR) % GRID_HEIGHT;
    if (posX >= 0 && posX < GRID_WIDTH && posY >= 0 && posY < GRID_HEIGHT) {
        grid[posY][posX] = 'R';  // On marquera le robot par "R"
    }

    // Récupérer la liste des objets et les afficher sur la grille
    BC_List *objects = bc_radar_ping(conn);
    while (objects) {
        BC_MapObject *obj = (BC_MapObject *) bc_ll_value(objects);
        int objX = (int)(obj->position.x / SCALE_FACTOR) % GRID_WIDTH;
        int objY = (int)(obj->position.y / SCALE_FACTOR) % GRID_HEIGHT;

        // On vérifie que l'objet est bien dans les limites de la grille
        if (objX >= 0 && objX < GRID_WIDTH && objY >= 0 && objY < GRID_HEIGHT) {
            // En fonction du type d'objet, on affiche un caractère différent
            if (obj->type == OT_WALL) {
                grid[objY][objX] = '#';
            } else if (obj->type == OT_BOOST) {
                grid[objY][objX] = 'B';
            } else if (obj->type == OT_PLAYER && obj->id != me.id) {
                grid[objY][objX] = 'E'; // E pour Ennemi
            }
        }
        objects = bc_ll_next(objects);
    }

    // Si la bibliothèque Battle-C nécessite de libérer la liste, on le ferait ici.
    // bc_ll_free(objects); // (À décommenter si la fonction existe)

    // Efface l'écran et dessine la grille ASCII
    printf("\033[H\033[J");
    printf("=== Vue du terrain ===\n");
    for (int i = 0; i < GRID_HEIGHT; i++) {
        for (int j = 0; j < GRID_WIDTH; j++) {
            printf("%c ", grid[i][j]);
        }
        printf("\n");
    }
    printf("======================\n");
}

// ---------------------------------------------------------
// updateMovement : calcule la direction du robot selon
// le principe des champs de potentiel (forces attractives/répulsives)
// ---------------------------------------------------------
void updateMovement(BC_Connection *conn) {
    // On récupère la position et la santé du bot
    BC_PlayerData me = bc_get_player_data(conn);
    BC_Vector3 currentPos = me.position;

    // On va additionner des forces sur l'axe X et Y (répulsion des murs, attraction bonus, etc.)
    double totalForceX = 0.0;
    double totalForceY = 0.0;

    // Je parcours la liste des objets pour calculer la force qu'ils exercent sur moi
    BC_List *envObjects = bc_radar_ping(conn);
    while (envObjects) {
        BC_MapObject *obj = (BC_MapObject *) bc_ll_value(envObjects);
        double dist = distanceBetween(currentPos, obj->position);

        // Force répulsive des obstacles (murs)
        if (obj->type == OT_WALL && dist < OBSTACLE_RADIUS && dist > 0.001) {
            double angleToObj = angleBetween(currentPos, obj->position);
            double pushForce = OBSTACLE_PUSH_FACTOR / dist;
            totalForceX -= pushForce * cos(angleToObj);
            totalForceY -= pushForce * sin(angleToObj);
        }
        // Force attractive des bonus (si ma santé est critique)
        else if (obj->type == OT_BOOST && me.health < CRITICAL_HEALTH && dist < BONUS_RADIUS && dist > 0.001) {
            double angleToBonus = angleBetween(currentPos, obj->position);
            double pullForce = BONUS_PULL_FACTOR / dist;
            totalForceX += pullForce * cos(angleToBonus);
            totalForceY += pullForce * sin(angleToBonus);
        }
        // Attraction (faible) vers les ennemis pour aider au tir
        else if (obj->type == OT_PLAYER && obj->id != me.id && dist < MAX_SHOOT_RANGE * 3 && dist > 0.001) {
            double angleToEnemy = angleBetween(currentPos, obj->position);
            double tentForce = ENEMY_TENT_FACTOR / dist;
            totalForceX += tentForce * cos(angleToEnemy);
            totalForceY += tentForce * sin(angleToEnemy);
        }

        envObjects = bc_ll_next(envObjects);
    }

    // bc_ll_free(envObjects); // Pareil, si nécessaire pour libérer la mémoire

    // Si je ne suis influencé par rien (aucune force), je pars dans une direction aléatoire
    if (fabs(totalForceX) < 0.0001 && fabs(totalForceY) < 0.0001) {
        double randomDirection = ((double)rand() / RAND_MAX) * 2 * M_PI;
        totalForceX = cos(randomDirection);
        totalForceY = sin(randomDirection);
    }

    // On normalise le vecteur de force pour qu'il ait une taille "BASE_SPEED"
    double magnitude = sqrt(totalForceX * totalForceX + totalForceY * totalForceY);
    if (magnitude > 0) {
        totalForceX = (totalForceX / magnitude) * BASE_SPEED;
        totalForceY = (totalForceY / magnitude) * BASE_SPEED;
    }

    // On envoie la vitesse au serveur
    bc_set_speed(conn, totalForceX, totalForceY, 0);
}

// ---------------------------------------------------------
// updateShooting : localise l'ennemi le plus proche et tente un tir
// ---------------------------------------------------------
void updateShooting(BC_Connection *conn) {
    BC_PlayerData me = bc_get_player_data(conn);
    BC_Vector3 myPos = me.position;

    BC_List *visibleObjects = bc_radar_ping(conn);
    double nearestDistance = MAX_SHOOT_RANGE;
    double bestAngle = 0.0;
    int enemyDetected = 0;

    // On cherche l'ennemi le plus proche dans un rayon déterminé (MAX_SHOOT_RANGE)
    while (visibleObjects) {
        BC_MapObject *obj = (BC_MapObject *) bc_ll_value(visibleObjects);
        if (obj->type == OT_PLAYER && obj->id != me.id) {
            double enemyDist = distanceBetween(myPos, obj->position);
            if (enemyDist < nearestDistance) {
                nearestDistance = enemyDist;
                bestAngle = angleBetween(myPos, obj->position);
                enemyDetected = 1;
            }
        }
        visibleObjects = bc_ll_next(visibleObjects);
    }

    // bc_ll_free(visibleObjects); // À décommenter si besoin

    // Si on a repéré un ennemi à portée
    if (enemyDetected) {
        // On doit convertir l'angle en degrés pour bc_shoot
        double angleDegrees = bestAngle * 180.0 / M_PI;
        BC_ShootResult shootResult = bc_shoot(conn, angleDegrees);
        if (shootResult.success) {
            printf("Coup réussi ! Dégâts infligés : %llu\n", shootResult.damage_points);
        } else {
            printf("Raté... Code erreur : %d\n", shootResult.fail_reason);
        }
    }
}

// ---------------------------------------------------------
// Gestion multithread : deux threads, un pour bouger, un pour tirer
// ---------------------------------------------------------
typedef struct {
    BC_Connection *conn;
} WorkerData;

// Thread pour la logique de déplacement
void *movementWorker(void *arg) {
    WorkerData *worker = (WorkerData *) arg;
    while (!stopThreads) {
        updateMovement(worker->conn);
        usleep(REFRESH_DELAY);
    }
    return NULL;
}

// Thread pour la logique de tir
void *shootingWorker(void *arg) {
    WorkerData *worker = (WorkerData *) arg;
    while (!stopThreads) {
        updateShooting(worker->conn);
        usleep(REFRESH_DELAY);
    }
    return NULL;
}

// ---------------------------------------------------------
// main : point d'entrée du programme
// ---------------------------------------------------------
int main() {
    // Initialisation aléatoire pour les directions random
    srand(time(NULL));

    // Se connecter au serveur Battle-C (changer l'IP/port au besoin)
    BC_Connection *conn = bc_connect("5.135.136.236", 8080);
    if (!conn) {
        fprintf(stderr, "Connexion au serveur échouée.\n");
        return EXIT_FAILURE;
    }

    // On met la vitesse du robot à 0 au départ (juste pour être propre)
    bc_set_speed(conn, 0, 0, 0);

    // Préparation des threads
    pthread_t threadMovement, threadShooting;
    WorkerData data = { conn };

    // Thread pour le mouvement
    if (pthread_create(&threadMovement, NULL, movementWorker, &data) != 0) {
        fprintf(stderr, "Erreur lors de la création du thread de déplacement.\n");
        bc_disconnect(conn);
        return EXIT_FAILURE;
    }

    // Thread pour le tir
    if (pthread_create(&threadShooting, NULL, shootingWorker, &data) != 0) {
        fprintf(stderr, "Erreur lors de la création du thread de tir.\n");
        bc_disconnect(conn);
        return EXIT_FAILURE;
    }

    // Boucle principale : tant que je suis vivant, j'affiche la carte de temps en temps
    int displayCounter = 0;
    while (1) {
        BC_PlayerData me = bc_get_player_data(conn);
        if (me.is_dead) {
            // Si je suis mort, j'arrête mes threads
            stopThreads = 1;
            break;
        }

        // Afficher la carte uniquement tous les DISPLAY_EVERY tours
        if (displayCounter >= DISPLAY_EVERY) {
            displayMap(conn);
            displayCounter = 0;
        } else {
            displayCounter++;
        }

        usleep(REFRESH_DELAY);
    }

    // On attend la fin des threads
    pthread_join(threadMovement, NULL);
    pthread_join(threadShooting, NULL);

    // Déconnexion propre du serveur
    bc_disconnect(conn);

    return EXIT_SUCCESS;
}