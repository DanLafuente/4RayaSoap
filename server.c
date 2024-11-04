#include "server.h"

/** Flag to enable debugging */
#define DEBUG_SERVER 1

/** Array with games */
tGame games[MAX_GAMES];

/** Mutex to protect the game status field in the array of games */
pthread_mutex_t mutexStatusArray = PTHREAD_MUTEX_INITIALIZER;


void initServerStructures(){

    if (DEBUG_SERVER)
        printf("Initializing...\n");

    // Init seed
    srand(time(NULL));

    // Init each game
    for (int i = 0; i < MAX_GAMES; i++)
		freeGameByIndex(i);      
}

conecta4ns__tPlayer switchPlayer(conecta4ns__tPlayer currentPlayer){
    return (currentPlayer == player1) ? player2 : player1;
}

int searchEmptyGame(){
	
	pthread_mutex_lock(&mutexStatusArray);
	int i = 0;
	while(i < MAX_GAMES && games[i].status == gameReady){
		++i;
	}
	pthread_mutex_unlock(&mutexStatusArray);

	return i;
}

int checkPlayer(xsd__string playerName, int gameId) {
	return strcmp(games[gameId].player1Name, playerName) == 0;
}

void freeGameByIndex(int i){

	// Allocate and init board
	games[i].board = (xsd__string) malloc (BOARD_WIDTH * BOARD_HEIGHT);
	initBoard(games[i].board);

	// Calculate the first player to play
	if ((rand() % 2) == 0)
		games[i].currentPlayer = player1;
	else
		games[i].currentPlayer = player2;

	// Allocate and init player names
	games[i].player1Name = (xsd__string) malloc (STRING_LENGTH);
	games[i].player2Name = (xsd__string) malloc (STRING_LENGTH);
	memset(games[i].player1Name, 0, STRING_LENGTH);
	memset(games[i].player2Name, 0, STRING_LENGTH);

	// Game status
	games[i].endOfGame = FALSE;

	// Protect variable to init game
	pthread_mutex_lock(&mutexStatusArray);
	games[i].status = gameEmpty;
	pthread_mutex_unlock(&mutexStatusArray);

	// Init mutex and condition variable
	pthread_mutex_init(&games[i].mutex, NULL);
	pthread_cond_init(&games[i].condition, NULL);
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
	if(DEBUG_SERVER)
		printf("Partida %d encontrada para el jugador %s\n", match, playerName.msg);

	if(match == MAX_GAMES){
		*code = ERROR_SERVER_FULL;
		return SOAP_OK;
	}
	*code = match;

	// Update game status
	if(games[match].status == gameEmpty){	// If match is empty we register the player1

		pthread_mutex_lock(&games[match].mutex);
		games[match].status = gameWaitingPlayer;
		pthread_mutex_unlock(&games[match].mutex);

		games[match].player1Name = malloc(sizeof(char) * playerName.__size);
		strncpy(games[match].player1Name, playerName.msg, playerName.__size);

		if(DEBUG_SERVER)
			printf("Durmiendo al jugador1\n");
		pthread_mutex_lock(&games[match].mutex);
			pthread_cond_wait(&games[match].condition, &games[match].mutex);
		pthread_mutex_unlock(&games[match].mutex);

		if(DEBUG_SERVER)
			printf("Jugador1 despierto\n");
	}
	else if(games[match].status == gameWaitingPlayer){	// Player1 already in match so we register player2
		if(checkPlayer(playerName.msg, match)) {
			if(DEBUG_SERVER)
				printf("Nombres repetidos\n");
			*code = ERROR_PLAYER_REPEATED;
			return SOAP_OK;
		}

		if(DEBUG_SERVER)
			printf("Despertando al jugador1\n");

		if(DEBUG_SERVER){
			if(games[match].currentPlayer == player1)
				printf("Player1 starts!\n");
			else
				printf("Player2 starts!\n");	
		}

		// Save new game data
		games[match].player2Name = malloc(sizeof(char) * playerName.__size);
		strncpy(games[match].player2Name, playerName.msg, playerName.__size);
		pthread_cond_signal(&games[match].condition);

		// Declare the game as started
		pthread_mutex_lock(&games[match].mutex);
		games[match].status = gameReady;
		pthread_mutex_unlock(&games[match].mutex);
	}

	if (DEBUG_SERVER)
		printf ("[Register] Registering new player -> [%s] on game %d\n", playerName.msg, match);

	return SOAP_OK;
}

int conecta4ns__getStatus(struct soap *soap, conecta4ns__tMessage playerName, int gameId, conecta4ns__tBlock* status){

	char message[30];

	// Alloc memory for the status
	allocClearBlock(soap, status);
	
	if(checkWinner(games[gameId].board, games[gameId].currentPlayer)){
		copyGameStatusStructure(status, "You win!\0", games[gameId].board, GAMEOVER_WIN);
		printf("Texto: %s\n", status->msgStruct.msg);
		freeGameByIndex(gameId);
		return SOAP_OK;
	}
	else if(isBoardFull(games[gameId].board)){
		copyGameStatusStructure(status, "Draw!\0", games[gameId].board, GAMEOVER_DRAW);
		freeGameByIndex(gameId);
		return SOAP_OK;
	}


	if (DEBUG_SERVER)
		printf("Receiving getStatus() request from -> %s in game %d\n", playerName.msg, gameId);

	// Select the current player
	conecta4ns__tPlayer player = checkPlayer(playerName.msg, gameId) ? player1 : player2;
	char playerChip = (player == player1) ? PLAYER_1_CHIP : PLAYER_2_CHIP;
	sprintf(&message, "It's your turn! Your chip is %c", playerChip);

	// Block the player who does not move
	pthread_mutex_lock(&games[gameId].mutex);

		if(DEBUG_SERVER)
			printf("El jugador %s esta esperando...\n", playerName.msg);

		while(!games[gameId].endOfGame && games[gameId].currentPlayer != player){
			pthread_cond_wait(&games[gameId].condition, &games[gameId].mutex);
		}

		if(DEBUG_SERVER)
			printf("El jugador %s ahora esta activo!\n", playerName.msg);

		// Check if it is our turn or it we have lost or is a draw
		if(checkWinner(games[gameId].board, games[gameId].currentPlayer))
			copyGameStatusStructure(status, "You lose!\0", games[gameId].board, GAMEOVER_LOSE);
		else if(isBoardFull(games[gameId].board))
			copyGameStatusStructure(status, "Draw!\0", games[gameId].board, GAMEOVER_DRAW);
		else
			copyGameStatusStructure(status, &message, games[gameId].board, TURN_MOVE);
	pthread_mutex_unlock(&games[gameId].mutex);

	return SOAP_OK;
}

int conecta4ns__insertChip(struct soap *soap, conecta4ns__tMessage playerName, int matchID, int column, int* resCode){

	if(checkMove(games[matchID].board, column) ==  fullColumn_move){
		*resCode = TURN_REPEAT;
		return SOAP_OK;
	}

	conecta4ns__tPlayer player = checkPlayer(playerName.msg, matchID) ? player1 : player2;
	insertChip(games[matchID].board, player, column);
	
	if(checkWinner(games[matchID].board, games[matchID].currentPlayer)){
		*resCode = GAMEOVER_WIN;
		games[matchID].endOfGame = TRUE;
		if(DEBUG_SERVER)
			printf("%s from game %d has won the match\n", playerName.msg, matchID);
	}
	else{
		if(isBoardFull(games[matchID].board)){
			*resCode = GAMEOVER_DRAW;
			games[matchID].endOfGame = TRUE;
		}
		else{	// Only here we change the turn -> having the winner in the currentPlayer
			games[matchID].currentPlayer = switchPlayer(player);
			*resCode = TURN_WAIT;
		}
	}

	// Wake up the other player
	pthread_mutex_lock(&games[matchID].mutex);
		pthread_cond_signal(&games[matchID].condition);
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