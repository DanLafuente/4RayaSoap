#include "server.h"

/** Flag to enable debugging */
#define DEBUG_SERVER 1

/** Array with games */
tGame games[MAX_GAMES];

/** Mutex to protect the game status field in the array of games */
pthread_mutex_t mutexStatusArray = PTHREAD_MUTEX_INITIALIZER;


void initServerStructures(){

    if (DEBUG_SERVER)
        printf ("Initializing...\n");

    // Init seed
    srand (time(NULL));

    // Init each game
    for (int i = 0; i < MAX_GAMES; i++)
		freeGameByIndex(i);      
}

conecta4ns__tPlayer switchPlayer(conecta4ns__tPlayer currentPlayer){
    return (currentPlayer == player1) ? player2 : player1;
}

int searchEmptyGame(){
	
	int i = 0;
	while(i < MAX_GAMES && games[i].status == gameReady){
		++i;
	}

	if(DEBUG_SERVER)
		printf("Match found %d\n", i);

	return i;
}

int checkPlayer(xsd__string playerName, int gameId) {
	return strcmp(games[gameId].player1Name, playerName);
}

void freeGameByIndex(int i){

	// Allocate and init board
	games[i].board = (xsd__string) malloc (BOARD_WIDTH*BOARD_HEIGHT);
	initBoard (games[i].board);

	// Calculate the first player to play
	if ((rand() % 2) == 0)
		games[i].currentPlayer = player1;
	else
		games[i].currentPlayer = player2;

	// Allocate and init player names
	games[i].player1Name = (xsd__string) malloc (STRING_LENGTH);
	games[i].player2Name = (xsd__string) malloc (STRING_LENGTH);
	memset (games[i].player1Name, 0, STRING_LENGTH);
	memset (games[i].player2Name, 0, STRING_LENGTH);

	// Game status
	games[i].endOfGame = FALSE;
	games[i].status = gameEmpty;

	// Init mutex and cond variable
}

void copyGameStatusStructure(conecta4ns__tBlock* status, char* message, xsd__string board, int newCode){
    
    // Set the new code
    status->code = newCode;
    
    // Copy the message
    memset((status->msgStruct).msg, 0, STRING_LENGTH);
    strcpy ((status->msgStruct).msg, message);
    (status->msgStruct).__size = strlen ((status->msgStruct).msg);
    
    // Copy the board, only if it is not NULL
    if (board == NULL){
        status->board = NULL;
        status->__size = 0;
    }
    else{
        strncpy (status->board, board, BOARD_WIDTH * BOARD_HEIGHT);
        status->__size = BOARD_WIDTH * BOARD_HEIGHT;
    }
}

int conecta4ns__register(struct soap *soap, conecta4ns__tMessage playerName, int *code){

    int gameIndex = -1;
    int result = 0;

	// Set \0 at the end of the string
	playerName.msg[playerName.__size] = 0;
	
	// Search for a empty game
	int match = searchEmptyGame();
	if(match == MAX_GAMES){
		*code = ERROR_SERVER_FULL;
		return SOAP_OK;
	}
	*code = match;

	// Update game status
	if(games[match].status == gameEmpty){	// If match is empty we register the player1
		games[match].status = gameWaitingPlayer;
		games[match].player1Name = malloc(sizeof(char) * playerName.__size);
		strncpy(games[match].player1Name, playerName.msg, playerName.__size);

		printf("Jugador durmiendose\n");
		pthread_mutex_lock(&games[match].mutex);
			pthread_cond_wait(&games[match].condition, &games[match].mutex);
		pthread_mutex_unlock(&games[match].mutex);
		printf("Jugador despertado\n");
	}
	else{									// Player1 already in match so we register player2
		if(checkPlayer(playerName.msg, match)) {
			*code = ERROR_PLAYER_REPEATED;
			return SOAP_OK;
		}

		games[match].player2Name = malloc(sizeof(char) * playerName.__size);
		strncpy(games[match].player2Name, playerName.msg, playerName.__size);
		pthread_cond_signal(&games[match].condition);
		printf("Despertando al jugador1\n");
		games[match].status = gameReady;
	}
	
	if (DEBUG_SERVER){
		printf ("[Register] Registering new player -> [%s] on game %d\n", playerName.msg, match);
	}

	return SOAP_OK;
}

int conecta4ns__getStatus(struct soap *soap, conecta4ns__tMessage playerName, int gameId, conecta4ns__tBlock* status){

	char messageToPlayer[STRING_LENGTH];

	// Set \0 at the end of the string and alloc memory for the status
	playerName.msg[playerName.__size] = 0;
	allocClearBlock(soap, status);
	
	if (DEBUG_SERVER)
		printf ("Receiving getStatus() request from -> %s [%d] in game %d\n", playerName.msg, playerName.__size, gameId);

	conecta4ns__tPlayer player = checkPlayer(playerName.msg, gameId) ? player1 : player2;
	if(player != games[gameId].currentPlayer){
		pthread_mutex_lock(&games[gameId].mutex);
			pthread_cond_wait(&games[gameId].condition, &games[gameId].mutex);
		pthread_mutex_unlock(&games[gameId].mutex);
	}


	return SOAP_OK;
}

int conecta4ns__insertChip(struct soap *soap, conecta4ns__tMessage playerName, int matchID, int column, int* resCode){

	// Comprobar que no se quiere colocar en una columna llena
	if(!checkMove(games[matchID].board, column)){
		*resCode = TURN_MOVE;
		return SOAP_OK;
	}	

	conecta4ns__tPlayer player = checkPlayer(playerName.msg, matchID) ? player1 : player2;
	insertChip(games[matchID].board, player, column);
	if(checkWinner(games[matchID].board, player)){
		// Mandar codigo de ganador
	}
	else{
		// Mandar codigo de espera
	}

	games[matchID].currentPlayer = switchPlayer(player);
	pthread_mutex_lock(&games[matchID].mutex);
		pthread_cond_signal(&games[matchID].condition);	// Wake up the other player to make a play
	pthread_mutex_unlock(&games[matchID].mutex);

	return SOAP_OK;
}

void *processRequest(void *soap){

	pthread_detach(pthread_self());

	if (DEBUG_SERVER)
		printf ("Processing a new request...");

	soap_serve((struct soap*)soap);
	soap_destroy((struct soap*)soap);
	soap_end((struct soap*)soap);
	soap_done((struct soap*)soap);
	free(soap);

	return NULL;
}

int main(int argc, char **argv){ 

	struct soap soap;
	struct soap *tsoap;
	pthread_t tid;
	int port;
	SOAP_SOCKET m, s;

	// Init soap environment
	soap_init(&soap);

	// Configure timeouts
	soap.send_timeout = 60; // 60 seconds
	soap.recv_timeout = 60; // 60 seconds
	soap.accept_timeout = 3600; // server stops after 1 hour of inactivity
	soap.max_keep_alive = 100; // max keep-alive sequence

	initServerStructures();
	pthread_mutex_init(&mutexStatusArray, NULL);

	// Get listening port
	port = atoi(argv[1]);

	// Bind
	m = soap_bind(&soap, NULL, port, 100);

	if (!soap_valid_socket(m)){
		exit(1);
	}

	printf("Server is ON ...\n");
	while (TRUE){

		// Accept a new connection
		s = soap_accept(&soap);

		// Socket is not valid :(
		if (!soap_valid_socket(s)){

			if (soap.errnum){
				soap_print_fault(&soap, stderr);
				exit(1);
			}

			fprintf(stderr, "Time out!\n");
			break;
		}

		// Copy the SOAP environment
		tsoap = soap_copy(&soap);

		if (!tsoap){
			printf ("SOAP copy error!\n");
			break;
		}

		// Create a new thread to process the request
		pthread_create(&tid, NULL, (void*(*)(void*))processRequest, (void*)tsoap);
	}

	// Detach SOAP environment
	soap_done(&soap);
	return 0;
}