#include "battle_c.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"

void showPlayerData(BC_PlayerData playerData){
  printf("ID: %d\n");
  printf("Position: (%d,%d,%d)\n", playerData.position.x, playerData.position.y, playerData.position.z);
  printf("Vitesse: (%d,%d,%d)\n", playerData.speed.x, playerData.speed.y, playerData.speed.z);
  printf("Health: %d\n", playerData.health);
  printf("Score: %d\n", playerData.score);
  printf("Armor: %d\n", playerData.armor);
  printf("Mort: %s\n", playerData.is_dead ? "Yes" : "No");
  }


void radar_ping(BC_Connection *conn) {
  BC_List *objects = bc_radar_ping(conn);
  while (objects) {
    BC_MapObject *object = (BC_MapObject *)bc_ll_value(objects);
    printf("Objet détecté : ID=%d, Type=%d, Position=(%d, %d, %d)\n",
           object->id, object->type,
           object->position.x, object->position.y, object->position.z);
    objects = bc_ll_next(objects);
  }
}


int main(int argc, char *argv[]) {
  // Connexion
    BC_Connection *conn = bc_connect("5.135.136.236", 8080);

  // Récupération des données du joueur
    BC_PlayerData playerData = bc_get_player_data(conn);
    showPlayerData(playerData);

  // Scan radar
  radar_ping(conn);
}
