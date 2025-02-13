// Server.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <map>
#include <string>
#include <process.h>
#pragma comment(lib, "Ws2_32.lib")
using namespace std;

#include "declare.h"
#include "const.h"
#include "struct.h"
#include "utilities.h"

map <string, pair<string, int>> accountMap;
GAME							games[GAME_NUM];
DWORD							nEvents = 0;
WSAEVENT						events[WSA_MAXIMUM_WAIT_EVENTS];
PLAYER							players[WSA_MAXIMUM_WAIT_EVENTS];


unsigned _stdcall timelyUpdate(void* param) {
	char buff[BUFF_SIZE];
	char reserveBuff[BUFF_SIZE];
	int loopCount = 1;
	long long delay = 0;
	GAME currGame = (GAME)param;

	while (1) {
		long long currTime = loopCount * LOOP_TIME;
		if ((getTime() - currGame->startAt) >= currTime) {
			if (loopCount >= MAX_LOOP) { 
				informEndGame(currGame, (char*) UPDATE_GAME, (char*) UPDATE_GAME_OVER, buff, reserveBuff);
				resetGame(currGame);
				break;
			} 
			else { 
				//Enter critical section
				while (!TryEnterCriticalSection(&currGame->criticalSection)) {}
				for (int i = 0; i < CASTLE_NUM; i++) {
					if (currGame->castles[i]->occupiedBy > -1) {
					currGame->teams[currGame->castles[i]->occupiedBy]->gold += ADDITION_GOLD; 
					}
				}
				for (int i = 0; i < MINE_NUM; i++) {
					currGame->mines[i]->resources[WOOD] += ADDITION_WOOD; 
					currGame->mines[i]->resources[STONE] += ADDITION_STONE; 
					currGame->mines[i]->resources[IRON] += ADDITION_IRON; 
				}

				informUpdate(currGame, (char*) TIMELY_UPDATE, buff, reserveBuff);

				//Leave critical section
				LeaveCriticalSection(&currGame->criticalSection);

				delay = getTime() - currGame->startAt - currTime;
				loopCount++;
			}
		}

		Sleep(LOOP_TIME - delay); //Wait till next update
	}	

	return 0;
}

int main(int argc, char* argv[])
{
	DWORD							index;
	WSANETWORKEVENTS				sockEvent;
	char							buff[BUFF_SIZE];
	char							reserveBuff[BUFF_SIZE];

	//Step 1: Initiate WinSock
	WSADATA wsaData;
	WORD wVersion = MAKEWORD(2, 2);
	if (WSAStartup(wVersion, &wsaData)) {
		printf("Winsock 2.2 is not supported\n");
		return 0;
	}

	//Step 2: Construct LISTEN socket	
	SOCKET listenSock;
	listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	//Step 3: Bind address to socket
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, SERVER_ADDR, &serverAddr.sin_addr);

	players[0] = (PLAYER)malloc(sizeof(_player));
	players[0]->socket = listenSock;
	events[0] = WSACreateEvent(); //create new events
	nEvents++;

	// Associate event types FD_ACCEPT and FD_CLOSE
	// with the listening socket and newEvent   
	WSAEventSelect(players[0]->socket, events[0], FD_ACCEPT | FD_CLOSE);


	if (bind(listenSock, (sockaddr *)&serverAddr, sizeof(serverAddr)))
	{
		printf("Error %d: Cannot associate a local address with server socket.", WSAGetLastError());
		return 0;
	}

	//Step 4: Listen request from client
	if (listen(listenSock, 10)) {
		printf("Error %d: Cannot place server socket in state LISTEN.", WSAGetLastError());
		return 0;
	}

	printf("Server started!\n");
	loadAccountMap((char*) ACCOUNT_FILE);

	SOCKET connSock;
	sockaddr_in clientAddr;
	char clientIP[INET_ADDRSTRLEN];
	int clientAddrLen = sizeof(clientAddr);
	int ret, i, clientPort;

	for (i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; i++) {
		players[i] = (PLAYER)malloc(sizeof(_player));
		players[i]->index = i;
		updatePlayerInfo(players[i], 0, 0, 0, 0, 0, 0, 0, 0, NOT_AUTHORIZED);
	}

	for (i = 0; i < GAME_NUM; i++) {
		games[i] = (GAME)malloc(sizeof(_game));
		createEmptyGame(games[i]);
	}
	while (1) {
		//wait for network events on all socket
		index = WSAWaitForMultipleEvents(nEvents, events, FALSE, WSA_INFINITE, FALSE);
		if (index == WSA_WAIT_FAILED) {
			printf("Error %d: WSAWaitForMultipleEvents() failed\n", WSAGetLastError());
			break;
		}

		index = index - WSA_WAIT_EVENT_0;
		WSAEnumNetworkEvents(players[index]->socket, events[index], &sockEvent);

		if (sockEvent.lNetworkEvents & FD_ACCEPT) {
			if (sockEvent.iErrorCode[FD_ACCEPT_BIT] != 0) {
				printf("FD_ACCEPT failed with error %d\n", sockEvent.iErrorCode[FD_READ_BIT]);
				continue;
			}

			if ((connSock = accept(players[index]->socket, (sockaddr *)&clientAddr, &clientAddrLen)) == INVALID_SOCKET) {
				printf("Error %d: Cannot permit incoming connection.\n", WSAGetLastError());
				continue;
			}

			inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
			clientPort = ntohs(clientAddr.sin_port);

			printf("Accept connection from client [%s:%d]!\n", clientIP, clientPort);

			//Add new client into client array
			if (nEvents == WSA_MAXIMUM_WAIT_EVENTS) {
				printf("\nToo many clients.");
				closesocket(connSock);
			}
			else { // Add to last
				updatePlayerInfo(players[nEvents], connSock, clientIP, clientPort, 0, 0, 0, 0, 0, NOT_AUTHORIZED);
				events[nEvents] = WSACreateEvent();
				WSAEventSelect(players[nEvents]->socket, events[nEvents], FD_READ | FD_CLOSE);
				nEvents++;
			}

			//reset event
			WSAResetEvent(events[index]);
			continue;
		}

		if (sockEvent.lNetworkEvents & FD_READ) {
			//Receive message from client
			if (sockEvent.iErrorCode[FD_READ_BIT] != 0) {
				printf("FD_READ failed with error %d\n", sockEvent.iErrorCode[FD_READ_BIT]);
				break;
			}
			ret = Communicate(players[index], buff, reserveBuff);
			//Release socket and event if an error occurs
			if (ret <= 0) {
				closesocket(players[index]->socket);
				clearPlayerInfo(players[index], buff);
				WSACloseEvent(events[index]);
				if (index != nEvents - 1) Swap(players[index], players[nEvents - 1], events[index], events[nEvents - 1]); // Swap last event and socket with the empties
				nEvents--;
				continue;
			};
			continue;
		}

		if (sockEvent.lNetworkEvents & FD_CLOSE) {
			if (sockEvent.iErrorCode[FD_CLOSE_BIT] != 0) {
				printf("FD_CLOSE failed with error %d from client [%s:%d]\n", sockEvent.iErrorCode[FD_CLOSE_BIT], players[index]->IP, players[index]->port);
			}
			//Release socket and event
			closesocket(players[index]->socket);
			clearPlayerInfo(players[index], buff);
			WSACloseEvent(events[index]);
			if (index != nEvents - 1) Swap(players[index], players[nEvents - 1], events[index], events[nEvents - 1]);
			nEvents--;
		}
	}
	return 0;
}

/* The swap function replace just-disconnected player with last player in array
* @param	emptyPlayer		[IN/OUT]	Just-disconnected player
* @param	lastPlayer		[IN/OUT]	Last player in array
* @param	emptyEvent		[IN/OUT]	Event bind with emptyPlayer
* @param	lastEvent		[IN/OUT]	Event bind with lastPlayer
* @return	nothing
*/
void Swap(PLAYER emptyPlayer, PLAYER lastPlayer, WSAEVENT emptyEvent, WSAEVENT lastEvent) {
	// Copy lastPlayer to emptyPlayer
	updatePlayerInfo(emptyPlayer, lastPlayer->socket, lastPlayer->IP, lastPlayer->port, lastPlayer->teamIndex, lastPlayer->placeInTeam, lastPlayer->gameIndex, lastPlayer->game, lastPlayer->account, lastPlayer->state);
	if (emptyPlayer->game != NULL) { // Replace lastPlayer with emptyPlayer in lastPlayer's game and team
		GAME playerGame = emptyPlayer->game;
		playerGame->players[emptyPlayer->gameIndex] = emptyPlayer;
		playerGame->teams[emptyPlayer->teamIndex]->players[emptyPlayer->placeInTeam] = emptyPlayer;
	}
	emptyEvent = WSACreateEvent(); // Recreate event
	WSAEventSelect(emptyPlayer->socket, emptyEvent, FD_READ | FD_CLOSE); // Bind event with socket
	updatePlayerInfo(lastPlayer, 0, 0, 0, 0, 0, 0, 0, 0, NOT_AUTHORIZED); // Empty the lastPlayer
	WSACloseEvent(lastEvent); // Close lastEvent
}

/* The Communicate function communicate with a player
* @param	player		[IN]		Player's info
* @return	SOCKET_ERROR or number of received bytes
*/
int Communicate(PLAYER player, char * buff, char *reserveBuff) {
	int received;
	int length;
	char opcode[4];
	memset(buff, 0, BUFF_SIZE);
	received = Receive(player, buff, HEADER_SIZE, 0); // Receive header
	if (received == SOCKET_ERROR) {
		return SOCKET_ERROR;
	}
	buff[HEADER_SIZE] = 0;
	opcode[0] = buff[0];
	opcode[1] = buff[1];
	opcode[2] = buff[2];
	opcode[3] = 0;
	if (buff[HEADER_SIZE - 2] == 0 || (buff[HEADER_SIZE - 2] + 255) % 256 > 16) return SOCKET_ERROR;
	length = ((buff[HEADER_SIZE - 2] + 255) % 256) * 255 + (buff[HEADER_SIZE - 1] + 255) % 256; // Calculate payload length
	received = receiveAndProcessPayload(player, opcode, length, buff, reserveBuff);
	return received;
}

/* The send() wrapper function*/
int Send(PLAYER player, char *buff, int size, int flags) {
	int ret;
	int sent = 0;
	int turn = 0;
	while (sent < size) {
		ret = send(player->socket, buff + sent, size - sent, flags);
		if (ret == SOCKET_ERROR) {
			if (turn > 10) {
				printf("Error %d: Cannot send to player [%s:%d], disconnect!", WSAGetLastError(), player->IP, player->port);
				return SOCKET_ERROR;
			}
			else if (WSAGetLastError() == WSAEWOULDBLOCK) {
				turn++;
				continue;
			}
			else {
				printf("Error %d: Cannot send to player [%s:%d], disconnect!", WSAGetLastError(), player->IP, player->port);
				return SOCKET_ERROR;
			}
		}
		else sent += ret;
	}
	return sent;
}

/* The recv() wrapper function*/
int Receive(PLAYER player, char *buff, int size, int flags) {
	int ret;
	int received = 0;
	int turn = 0;
	while (received < size) {
		ret = recv(player->socket, buff + received, size - received, flags);
		if (ret == SOCKET_ERROR) {
			if (turn > 10) {
				printf("Error %d: Cannot receive to player [%s:%d], disconnect!", WSAGetLastError(), player->IP, player->port);
				return SOCKET_ERROR;
			}
			else if (WSAGetLastError() == WSAEWOULDBLOCK) {
				turn++;
				continue;
			}
			else {
				printf("Error %d: Cannot receive from player [%s:%d], disconnect!", WSAGetLastError(), player->IP, player->port);
				return SOCKET_ERROR;
			}
		}
		else received += ret;
	}
	return received;
}

/* The receiveAndProcessPayload function receives and processes payload
* @param	player		[IN]		Player's info
* @param	opcode		[IN]		Operation code
* @param	length		[IN]		Size of payload
* @param	buff		[IN/OUT]	Buffer to handle
* @return	SOCKET_ERROR or process result
*/
int receiveAndProcessPayload(PLAYER player, char *opcode, int length, char *buff, char *reserveBuff) {
	int ret = 0;
	if (length > 0) ret = Receive(player, buff, length, 0);
	buff[ret] = 0;
	if (ret == SOCKET_ERROR) return SOCKET_ERROR;
	if (strcmp(opcode, LOGIN) == 0) {
		printf("Handle login request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleLogin(player, opcode, buff);
	}
	else if (strcmp(opcode, SIGNUP) == 0) {
		printf("Handle signup request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleSignUp(player, opcode, buff);
	}
	else if (strcmp(opcode, CREATE_GAME) == 0) {
		printf("Handle create game request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleCreateGame(player, opcode, buff);
	}
	else if (strcmp(opcode, GET_LOBBY) == 0) {
		printf("Handle get lobby request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleGetLobby(player, opcode, buff);
	}
	else if (strcmp(opcode, JOIN_GAME) == 0) {
		printf("Handle join game request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleJoinGame(player, opcode, buff);
	}
	else if (strcmp(opcode, CHANGE_TEAM) == 0) {
		printf("Handle change team request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleChangeTeam(player, opcode, buff);
	}
	else if (strcmp(opcode, READY_PLAY) == 0) {
		printf("Handle ready request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleReadyPlay(player, opcode, buff);
	}
	else if (strcmp(opcode, UNREADY_PLAY) == 0) {
		printf("Handle unready request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleUnreadyPlay(player, opcode, buff);
	}
	else if (strcmp(opcode, QUIT_GAME) == 0) {
		printf("Handle quit game request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleQuitGame(player, opcode, buff);
	}
	else if (strcmp(opcode, START_GAME) == 0) {
		printf("Handle start game request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleStartGame(player, opcode, buff, reserveBuff);
	}
	else if (strcmp(opcode, KICK) == 0) {
		printf("Handle kick request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleKick(player, opcode, buff);
	}
	else if (strcmp(opcode, LOGOUT) == 0) {
		printf("Handle log out request from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleLogOut(player, opcode, buff);
	}
	else if (strcmp(opcode, ATTACK_CASTLE) == 0) {
		printf("Handle attack castle from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleAttackCastle(player, opcode, buff, reserveBuff);
	}
	else if (strcmp(opcode, ATTACK_MINE) == 0) {
		printf("Handle attack mine from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleAttackMine(player, opcode, buff, reserveBuff);
	}
	else if (strcmp(opcode, BUY_WEAPON) == 0) {
		printf("Handle buy weapon from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleBuyWeapon(player, opcode, buff, reserveBuff);
	}
	else if (strcmp(opcode, BUY_WALL) == 0) {
		printf("Handle buy wall from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = handleBuyWall(player, opcode, buff, reserveBuff);
	}
	else if (strcmp(opcode, CHEAT) == 0) {
		printf("Guess who is cheating? Player[%s:%d]: %s", player->IP, player->port, buff);
		ret = handleCheat(player, opcode, buff, reserveBuff);
	}
	else {
		printf("Unknown header from player [%s:%d]: %s\n", player->IP, player->port, buff);
		ret = setResponseAndSend(player, opcode, 0, 0, buff);
	}
	return ret;
}

/* The setResponseAndSend function sets response and sends to player
* @param	player				[IN]		Player's info
* @param	opcode				[IN]		Operation code
* @param	responsePayload		[IN]		Response buffer
* @param	responsePayloadLen	[IN]		Size of response
* @param	buff				[IN]		Buffer to send
* @return	Send result
*/
int setResponseAndSend(PLAYER player, char *opcode, char *responsePayload, int responsePayloadLen, char *buff) {
	// Add opcode
	buff[0] = opcode[0];
	buff[1] = opcode[1];
	buff[2] = opcode[2];
	// Calculate payload length
	buff[3] = responsePayloadLen / 255 + 1;
	buff[4] = responsePayloadLen % 255 + 1;
	if (responsePayload != 0) strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responsePayload);
	printf("Response to player [%s:%d] %s request: %s\n", player->IP, player->port, opcode, buff + HEADER_SIZE);
	return Send(player, buff, 5 + responsePayloadLen, 0);
}

/* The setResponseAndSend function sets response, used to set response to send to multiple player
* @param	opcode				[IN]		Operation code
* @param	responsePayload		[IN]		Response buffer
* @param	responsePayloadLen	[IN]		Size of response
* @param	buff				[IN/OUT]	Buffer contain full message
* @return	Nothing
*/
void setResponse(char *opcode, char *responsePayload, int responsePayloadLen, char *buff) {
	// Add header
	buff[0] = opcode[0];
	buff[1] = opcode[1];
	buff[2] = opcode[2];
	// Calculate payload length
	buff[3] = responsePayloadLen / 255 + 1;
	buff[4] = responsePayloadLen % 255 + 1;
	// Add payload to buffer
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responsePayload);
}

/* The handleLogin function handle login request from a player
* @param	player		[IN]		Player's info
* @param	opcode		[IN]		Operation code
* @param	buff		[IN]		Buffer to handle
* @return	Send Response result
*/
int handleLogin(PLAYER player, char *opcode, char *buff) {
	if (player->state != NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOGIN_E_ALREADY, strlen(LOGIN_E_ALREADY), buff);
	char *sharp = strchr(buff, '#');
	if (sharp == NULL || sharp + 1 == NULL) {
		return setResponseAndSend(player, opcode, (char*) LOGIN_E_NOTEXIST, strlen(LOGIN_E_NOTEXIST), buff);
	}
	unsigned int usernameLen = (int)(sharp - buff);
	unsigned int passwordLen = strlen(sharp + 1);
	if (usernameLen > ACCOUNT_SIZE || passwordLen > ACCOUNT_SIZE) {
		return setResponseAndSend(player, opcode, (char*) LOGIN_E_NOTEXIST, strlen(LOGIN_E_NOTEXIST), buff);
	}
	// Get username, password from payload
	char username[ACCOUNT_SIZE];
	char password[ACCOUNT_SIZE];
	sharp[0] = 0;
	strcpy_s(username, ACCOUNT_SIZE, buff);
	strcpy_s(password, ACCOUNT_SIZE, sharp + 1);
	// Find username
	map<string, pair<string, int>>::iterator it;
	it = accountMap.find(username);
	if (it == accountMap.end()) return setResponseAndSend(player, opcode, (char*) LOGIN_E_NOTEXIST, strlen(LOGIN_E_NOTEXIST), buff);
	else if (strcmp(it->second.first.c_str(), password) != 0) return setResponseAndSend(player, opcode, (char*) LOGIN_E_PASSWORD, strlen(LOGIN_E_PASSWORD), buff);
	else if (it->second.second == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOGIN_E_ELSEWHERE, strlen(LOGIN_E_ELSEWHERE), buff);
	else {
		// Update player state and account state in account map
		updatePlayerInfo(player, player->socket, player->IP, player->port, 0, 0, 0, 0, username, AUTHORIZED);
		it->second.second = AUTHORIZED;
		return setResponseAndSend(player, opcode, (char*) LOGIN_SUCCESS, strlen(LOGIN_SUCCESS), buff);
	}
}

/* The handleSignUp function handle signup request from a player
* @param	player		[IN]		Player's info
* @param	opcode		[IN]		Operation code
* @param	buff		[IN]		Buffer to handle
* @return	Send Response result
*/
int handleSignUp(PLAYER player, char *opcode, char *buff) {
	if (player->state != NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) SIGNUP_E_LOGGEDIN, strlen(SIGNUP_E_LOGGEDIN), buff);
	char *sharp = strchr(buff, '#');
	if (sharp == NULL || sharp + 1 == NULL) {
		return setResponseAndSend(player, opcode, (char*) SIGNUP_E_FORMAT, strlen(SIGNUP_E_FORMAT), buff);
	}
	unsigned int usernameLen = (int)(sharp - buff);
	unsigned int passwordLen = strlen(sharp + 1);
	if (usernameLen > ACCOUNT_SIZE || passwordLen > ACCOUNT_SIZE) {
		return setResponseAndSend(player, opcode, (char*) SIGNUP_E_FORMAT, strlen(SIGNUP_E_FORMAT), buff);
	}
	// Get username, password from payload
	char username[ACCOUNT_SIZE];
	char password[ACCOUNT_SIZE];
	sharp[0] = 0;
	strcpy_s(username, ACCOUNT_SIZE, buff);
	strcpy_s(password, ACCOUNT_SIZE, sharp + 1);
	if (strlen(password) < passwordLen) return setResponseAndSend(player, opcode, (char*) SIGNUP_E_FORMAT, strlen(SIGNUP_E_FORMAT), buff);
	map<string, pair<string, int>>::iterator it;
	it = accountMap.find(username);
	if (it != accountMap.end()) return setResponseAndSend(player, opcode, (char*) SIGNUP_E_EXIST, strlen(SIGNUP_E_EXIST), buff);
	else {
		// Add new account to account map and to account file
		accountMap[username] = make_pair(password, NOT_AUTHORIZED);
		addNewAccount((char*) ACCOUNT_FILE, username, password);
		return setResponseAndSend(player, opcode, (char*) SIGNUP_SUCCESS, strlen(SIGNUP_SUCCESS), buff);
	}
}

/* The handleCreateGame function handle create game request from a player, include validate, create game (include create a new game, connect game and player)
* @param	player		[IN]		Player's info
* @param	opcode		[IN]		Operation code
* @param	buff		[IN]		Buffer to handle
* @return	Send Response result
*/
int handleCreateGame(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) CREATE_E_NOTAUTH, strlen(CREATE_E_NOTAUTH), buff);
	else if (player->state != AUTHORIZED) return setResponseAndSend(player, opcode, (char*) CREATE_E_INGAME, strlen(CREATE_E_INGAME), buff);
	else {
		int i;
		// Get number of teams
		int teamNum = buff[0] - 48;
		if ((teamNum < 2) || (teamNum > 4)) return setResponseAndSend(player, opcode, (char*) CREATE_E_INVALIDTEAM, strlen(CREATE_E_INVALIDTEAM), buff);
		for (i = 0; i < GAME_NUM; i++) {
			if (games[i]->id == 0) {
				// create new game with host player
				createGame(player, games[i], teamNum);
				break;
			}
		}
		if (i == GAME_NUM) return setResponseAndSend(player, opcode, (char*) CREATE_E_FULLGAME, strlen(CREATE_E_FULLGAME), buff);
		long long gameId = games[i]->id;
		memset(buff, 0, BUFF_SIZE);
		strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, CREATE_SUCCESS);
		strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
		_i64toa_s(gameId, buff + BUFFLEN, BUFF_SIZE, 10);
		return setResponseAndSend(player, opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	}
}

/* The handleGetLobby function handle get lobby request from a player
* @param	player		[IN]		Player's info
* @param	opcode		[IN]		Operation code
* @param	buff		[IN]		Buffer to handle
* @return	Send Response result
*/
int handleGetLobby(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOBBY_E_NOTAUTH, strlen(LOBBY_E_NOTAUTH), buff);
	else if (player->state != AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOBBY_E_INGAME, strlen(LOBBY_E_INGAME), buff);
	else {
		strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, LOBBY_SUCCESS);
		getLobbyList(buff);
		return setResponseAndSend(player, opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	}
}

/* The getLobbyList function get lobby list from all games, start with buff + BUFFLEN
* @param	buff		[IN/OUT]	Buffer to store lobby list
* @return	Nothing
*/
void getLobbyList(char *buff) {
	for (int i = 0; i < GAME_NUM; i++) {
		if (games[i]->id != 0 && games[i]->gameState == WAITING) {
			strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
			getGameInfo(games[i], buff);
		}
	}
}

/* The getGameInfo function handle get a single game info
* @param	game		[IN]		Game to get info
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	Nothing
*/
void getGameInfo(GAME game, char *buff) {
	_i64toa_s(game->id, buff + BUFFLEN, BUFF_SIZE, 10); // 13 bytes
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->teamNum, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#"); // 16 bytes
	getTeamPlayerString(game, buff + BUFFLEN);
}

/* The getTeamPlayerString function get game-team-player info, example: Player 0, 3, 7 in team 0, player 4, 5, 9 in team 2, result: 0xx022x0x2xx
* @param	game		[IN]		Game
* @param	buff		[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void getTeamPlayerString(GAME game, char *buff) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (game->players[i] == NULL) {
			strcat_s(buff, BUFF_SIZE, "x");
		}
		else {
			_itoa_s(game->players[i]->gameIndex, buff + i, BUFF_SIZE, 10);
		}
	}
}

/* The getGameRoomChangeSuccessResponse function create a response to send to all players in a room whenever someone join, leave, ready or unready
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in game
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void getGameRoomChangeSuccessResponse(GAME game, int requestPlayer, char* responseCode, char *buff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode); // 7 bytes
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#"); // 8 bytes
	_itoa_s(game->teamNum, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = requestPlayer + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	temp = BUFFLEN;
	buff[BUFFLEN] = game->host + 48;
	buff[temp + 1] = 0;
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (game->players[i] != NULL) {
			strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
			temp = BUFFLEN;
			_itoa_s(i, buff + BUFFLEN, BUFF_SIZE, 10);
			buff[temp + 1] = 0;
			strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
			strcpy_s(buff + BUFFLEN, BUFF_SIZE, game->players[i]->account);
			strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
			_itoa_s(game->players[i]->state, buff + BUFFLEN, BUFF_SIZE, 10);
			strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
			_itoa_s(game->players[i]->teamIndex, buff + BUFFLEN, BUFF_SIZE, 10);
		}
	};
}

/* The informGameRoomChange function create a response and send to all players in a room whenever someone join, leave, ready or unready
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in game
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informGameRoomChange(GAME game, int index, char *opcode, char *responseCode, char *buff) {
	getGameRoomChangeSuccessResponse(game, index, responseCode, buff);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendGameRoomMessageToAllPlayersInGameRoom(game, BUFFLEN, buff);
}

/* The informCastleAttack function create a message with game properties and mine question and send to all players in a room whenever a castle being attack
* @param	game				[IN]		Game
* @param	castleId			[IN]		Id of castle being attack
* @param	playerIndex			[IN]		Place of request player in game
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informCastleAttack(GAME game, int castleId, int playerIndex, char *opcode, char* responseCode, char * buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = playerIndex + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(castleId, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->castles[castleId]->wall->type, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->castles[castleId]->wall->defense, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	TEAM team = game->teams[game->players[playerIndex]->teamIndex];
	_itoa_s(team->weapon->type, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(team->weapon->attack, buff + BUFFLEN, BUFF_SIZE, 10);
	getCastleQuestion(game->castles[castleId], (char*) HARD_QUESTION_FILE, buff + BUFFLEN);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
}

/* The informMineAttack function create a message with game properties and new question and send to all players in a room whenever a mine being attack
* @param	game				[IN]		Game
* @param	mineId				[IN]		Mine's index in game->mines[]
* @param	resourceType		[IN]		Attack resource
* @param	castleId			[IN]		Id of castle being attack
* @param	playerIndex			[IN]		Place of request player in game
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informMineAttack(GAME game, int mineId, int resourceType, int playerIndex, char *opcode, char* responseCode, char * buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = playerIndex + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(mineId, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(resourceType, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->players[playerIndex]->teamIndex, buff + BUFFLEN, BUFF_SIZE, 10);
	getMineQuestion(game->mines[mineId], (char*) EASY_QUESTION_FILE, resourceType, buff + BUFFLEN);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
}

/* The informBuyWeapon function create a response and send to all players in a room whenever someone join, leave, ready or unready
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in team
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informBuyWeapon(GAME game, int playerIndex, int weaponId, char *opcode, char *responseCode, char *buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = playerIndex + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->players[playerIndex]->teamIndex, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(weaponId, buff + BUFFLEN, BUFF_SIZE, 10);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
};

/* The informBuyWeapon function create a response with game properties and send to all players in a room whenever someone join, leave, ready or unready
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in team
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informBuyWall(GAME game, int playerIndex, int castleId, int wallId, char *opcode, char *responseCode, char *buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = playerIndex + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	calculateACastle(game->castles[castleId], buff + BUFFLEN);
	_itoa_s(game->players[playerIndex]->teamIndex, buff + BUFFLEN, BUFF_SIZE, 10);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
};

void informCheat(GAME game, int playerIndex, char *opcode, char *responseCode, char *buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	int temp = BUFFLEN;
	buff[BUFFLEN] = playerIndex + 48;
	buff[temp + 1] = 0;
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(game->players[playerIndex]->teamIndex, buff + BUFFLEN, BUFF_SIZE, 10);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
}

/* The informUpdate function inform resource update to all player
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in team
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informUpdate(GAME game, char *opcode, char *buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	for (int i = 0; i < CASTLE_NUM; i++) {
		calculateACastle(game->castles[i], buff + BUFFLEN);
	}
	for (int i = 0; i < MINE_NUM; i++) {
		calculateAMine(game->mines[i], buff + BUFFLEN);
	}
	for (int i = 0; i < TEAM_NUM; i++) {
		_itoa_s(game->teams[i]->index, buff + BUFFLEN, BUFF_SIZE, 10);
		strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
		calculateATeam(game->teams[i], buff + BUFFLEN);
	}
	buff[strlen(buff) - 1] = 0;
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
};

/* The informEndGame function inform endgame signal to all player
* @param	game				[IN]		Game
* @param	requestPlayer		[IN]		Place of request player in team
* @param	responseCode		[IN]		Response code for request
* @param	buff				[IN/OUT]	Buffer to store result
* @return	Nothing
*/
void informEndGame(GAME game, char *opcode, char* responseCode, char *buff, char *reserveBuff) {
	memset(buff, 0, BUFF_SIZE);
	strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, responseCode);
	setResponse(opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
};


/* The sendNewCastleQuestion function send new castle question when game start
* @param	game				[IN]		Game
* @param	castleId			[IN]		Id of castle
* @param	buff				[IN]		Buffer to store result
* @return	Nothing
*/
void sendNewCastleQuestion(GAME game, int castleId, char *buff, char *reserveBuff) {
	memset(buff + HEADER_SIZE, 0, BUFF_SIZE - 5);
	strcpy_s(buff + BUFFLEN, BUFF_SIZE, UPDATE_GAME_CSTQUEST);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(castleId, buff + BUFFLEN, BUFF_SIZE, 10);
	getCastleQuestion(game->castles[castleId], (char*) HARD_QUESTION_FILE, buff + BUFFLEN);
	setResponse((char*) UPDATE_GAME, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
}

/* The sendNewCastleQuestion function send new mine question when game start
* @param	game				[IN]		Game
* @param	mineId				[IN]		Id of castle
* @param	type				[IN]		Resource type
* @param	buff				[IN]		Buffer to store result
* @return	Nothing
*/
void sendNewMineQuestion(GAME game, int mineId, int type, char *buff, char *reserveBuff) {
	memset(buff + HEADER_SIZE, 0, BUFF_SIZE - 5);
	strcpy_s(buff + BUFFLEN, BUFF_SIZE, UPDATE_GAME_MINEQUEST);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(mineId, buff + BUFFLEN, BUFF_SIZE, 10);
	strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
	_itoa_s(type, buff + BUFFLEN, BUFF_SIZE, 10);
	getMineQuestion(game->mines[mineId], (char*) EASY_QUESTION_FILE, type, buff + BUFFLEN);
	setResponse((char*) UPDATE_GAME, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
	sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
}

/* The sendToAllPlayersInGameRoom function send to all players in game room
* @param	game				[IN]		Game
* @param	responseLen			[IN]		Response length
* @param	buff				[IN]		Buffer to store result
* @return	Nothing
*/
void sendInGameMessageToAllPlayersInGameRoom(GAME game, int responseLen, char *buff, char *reserveBuff) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (game->players[i] != NULL) {
			if (Send(game->players[i], buff, responseLen, 0) == SOCKET_ERROR) {
				PLAYER otherPlayer = game->players[i];
				closesocket(otherPlayer->socket);
				clearPlayerInfo(otherPlayer, reserveBuff);
				WSACloseEvent(events[otherPlayer->index]);
				if (otherPlayer->index != nEvents - 1) Swap(otherPlayer, players[nEvents - 1], events[game->players[i]->index], events[nEvents - 1]); // Swap last event and socket with the empties
				nEvents--;
			}
		}
	}
}

void sendGameRoomMessageToAllPlayersInGameRoom(GAME game, int responseLen, char *buff) {
	for (int i = 0; i < PLAYER_NUM; i++) {
		if (game->players[i] != NULL) {
			if (Send(game->players[i], buff, responseLen, 0) == SOCKET_ERROR) {
				PLAYER otherPlayer = game->players[i];
				WSACloseEvent(events[otherPlayer->index]);
				closesocket(otherPlayer->socket);
				clearPlayerInfo(otherPlayer, buff);
				if (otherPlayer->index != nEvents - 1) Swap(otherPlayer, players[nEvents - 1], events[game->players[i]->index], events[nEvents - 1]); // Swap last event and socket with the empties
				nEvents--;
				break;
			}
		}
	}
}

/* The handleJoinGame function handle a join request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleJoinGame(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) JOIN_E_NOTAUTH, strlen(JOIN_E_NOTAUTH), buff);
	else if (player->state != AUTHORIZED) return setResponseAndSend(player, opcode, (char*) JOIN_E_ALREADY, strlen(JOIN_E_ALREADY), buff);
	else {
		char *sharp = strchr(buff, '#');
		if (sharp == NULL) return setResponseAndSend(player, opcode, (char*) JOIN_E_FORMAT, strlen(JOIN_E_FORMAT), buff);
		int distance = int(sharp - buff);
		if (distance != 13 || *(sharp + 2) != 0) return setResponseAndSend(player, opcode, (char*) JOIN_E_FORMAT, strlen(JOIN_E_FORMAT), buff);
		sharp[0] = 0;
		long long gameId = _atoi64(buff);
		int team = atoi(sharp + 1);
		int i, emptyPlaceInTeam, emptyPlaceInGame = -1, countPlayer = 0;
		GAME game = NULL;
		for (i = 0; i < GAME_NUM; i++) {
			if (games[i]->id == gameId) {
				game = games[i];
				break;
			}
		}
		if (i == GAME_NUM || game == NULL) return setResponseAndSend(player, opcode, (char*) JOIN_E_NOGAME, strlen(JOIN_E_NOGAME), buff);
		if (game->gameState != WAITING) return setResponseAndSend(player, opcode, (char*) JOIN_E_PLAYING, strlen(JOIN_E_PLAYING), buff);
		for (i = 0; i < PLAYER_NUM; i++) {
			if (game->players[i] == NULL) {
				if (emptyPlaceInGame == -1) emptyPlaceInGame = i;
			}
			else countPlayer++;
		}
		if (countPlayer == game->teamNum * 3) return setResponseAndSend(player, opcode, (char*) JOIN_E_FULLGAME, strlen(JOIN_E_FULLGAME), buff);
		if ((team < 0) || (team > game->teamNum - 1)) return setResponseAndSend(player, opcode, (char*) JOIN_E_NOTEAM, strlen(JOIN_E_NOTEAM), buff);
		for (i = 0; i < 3; i++) {
			if (game->teams[team]->players[i] == NULL) {
				emptyPlaceInTeam = i;
				break;
			}
		}
		if (i == 3)	return setResponseAndSend(player, opcode, (char*) JOIN_E_FULLTEAM, strlen(JOIN_E_FULLTEAM), buff);
		// Add player to team
		game->teams[team]->players[emptyPlaceInTeam] = player;
		// Add player to game
		game->players[emptyPlaceInGame] = player;
		// Update player info
		updatePlayerInfo(player, player->socket, player->IP, player->port, team, emptyPlaceInTeam, emptyPlaceInGame, game, player->account, JOINT);
		// Create response
		strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, JOIN_SUCCESS);
		strcat_s(buff + BUFFLEN, BUFF_SIZE, "#");
		int temp = BUFFLEN;
		buff[BUFFLEN] = emptyPlaceInGame + 48;
		buff[temp + 1] = 0;
		if (setResponseAndSend(player, opcode, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff) == SOCKET_ERROR) {
			WSACloseEvent(events[player->index]);
			closesocket(player->socket);
			clearPlayerInfo(player, buff);
			if (player->index != nEvents - 1) Swap(player, players[nEvents - 1], events[player->index], events[nEvents - 1]); // Swap last event and socket with the empties
			nEvents--;
			return 1;
		};
		informGameRoomChange(game, emptyPlaceInGame, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_JOIN, buff);
		return 1;
	}
}

/* The handleChangeTeam function handle a change team request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleChangeTeam(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) CHANGE_E_NOTAUTH, strlen(CHANGE_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) CHANGE_E_NOTINGAME, strlen(CHANGE_E_NOTINGAME), buff);
	else if (player->state == READY) return setResponseAndSend(player, opcode, (char*) CHANGE_E_READY, strlen(CHANGE_E_READY), buff);
	else if (player->state == PLAYING) return setResponseAndSend(player, opcode, (char*) CHANGE_E_PLAYING, strlen(CHANGE_E_PLAYING), buff);
	else {
		int emptyPlaceInTargetTeam = -1, targetTeam = buff[0] - 48;
		if (targetTeam < 0 || targetTeam > player->game->teamNum - 1) return setResponseAndSend(player, opcode, (char*) CHANGE_E_PLAYING, strlen(CHANGE_E_PLAYING), buff);
		if (targetTeam == player->teamIndex) return setResponseAndSend(player, opcode, (char*) CHANGE_E_CURRENTTEAM, strlen(CHANGE_E_CURRENTTEAM), buff);
		for (int i = 0; i < 3; i++) {
			if (player->game->teams[targetTeam]->players[i] == NULL) {
				emptyPlaceInTargetTeam = i;
				break;
			}
		}
		if (emptyPlaceInTargetTeam == -1) return setResponseAndSend(player, opcode, (char*) CHANGE_E_FULL, strlen(CHANGE_E_FULL), buff);
		GAME game = player->game;
		// empty player place in old team
		game->teams[player->teamIndex]->players[player->placeInTeam] = NULL;
		// link player to team
		player->teamIndex = targetTeam;
		player->placeInTeam = emptyPlaceInTargetTeam;
		// link team to player
		game->teams[targetTeam]->players[emptyPlaceInTargetTeam] = player;
		informGameRoomChange(game, player->gameIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_CHANGETEAM, buff);
		return 1;
	}
}

/* The handleReadyPlay function handle a ready play request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleReadyPlay(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) READY_E_NOTAUTH, strlen(READY_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) READY_E_NOTINGAME, strlen(READY_E_NOTINGAME), buff);
	else if (player->state == READY) return setResponseAndSend(player, opcode, (char*) READY_E_ALREADY, strlen(READY_E_ALREADY), buff);
	else if (player->state == PLAYING) return setResponseAndSend(player, opcode, (char*) READY_E_PLAYING, strlen(READY_E_PLAYING), buff);
	else {
		if (player->gameIndex == player->game->host) return setResponseAndSend(player, opcode, (char*) READY_E_HOST, strlen(READY_E_HOST), buff);
		player->state = READY; // Change player state to ready
		informGameRoomChange(player->game, player->gameIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_READY, buff);
		return 1;
	}
}

/* The handleUnreadyPlay function handle a unready request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleUnreadyPlay(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) UNREADY_E_NOTAUTH, strlen(UNREADY_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) UNREADY_E_NOTINGAME, strlen(UNREADY_E_NOTINGAME), buff);
	else if (player->state == PLAYING) return setResponseAndSend(player, opcode, (char*) UNREADY_E_PLAYING, strlen(UNREADY_E_PLAYING), buff);
	else {
		if (player->gameIndex == player->game->host) return setResponseAndSend(player, opcode, (char*) UNREADY_E_HOST, strlen(UNREADY_E_HOST), buff);
		if (player->state == JOINT) return setResponseAndSend(player, opcode, (char*) UNREADY_E_ALREADY, strlen(UNREADY_E_ALREADY), buff);
		player->state = JOINT;
		informGameRoomChange(player->game, player->gameIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_UNREADY, buff);
		return 1;
	}
}

/* The handleQuitGame function handle a quit request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleQuitGame(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) QUIT_E_NOTAUTH, strlen(QUIT_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) QUIT_E_NOTINGAME, strlen(QUIT_E_NOTINGAME), buff);
	else if (player->state == READY) return setResponseAndSend(player, opcode, (char*) QUIT_E_READY, strlen(QUIT_E_READY), buff);
	else {
		int i;
		GAME game = player->game;
		int playerIndex = player->gameIndex;
		// Unlink game to player
		game->players[playerIndex] = NULL;
		// Unlink team to player
		game->teams[player->teamIndex]->players[player->placeInTeam] = NULL;
		for (i = 0; i < PLAYER_NUM; i++) {
			if (game->players[i] != NULL) {
				break;
			}
		}
		if (i == PLAYER_NUM) emptyGame(game);
		else if (game->host == player->gameIndex) game->host = i; // Update host if player is host
		updatePlayerInfo(player, player->socket, player->IP, player->port, 0, 0, 0, 0, player->account, AUTHORIZED);
		if (setResponseAndSend(player, (char*) QUIT_GAME, (char*) QUIT_SUCCESS, strlen(QUIT_SUCCESS), buff) == SOCKET_ERROR) {
			WSACloseEvent(events[player->index]);
			closesocket(player->socket);
			clearPlayerInfo(player, buff);
			if (player->index != nEvents - 1) Swap(player, players[nEvents - 1], events[player->index], events[nEvents - 1]); // Swap last event and socket with the empties
			nEvents--;
			return 1;
		};
		informGameRoomChange(game, playerIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_QUIT, buff);
		return 1;
	}
}

/* The handleStartGame function handle a start request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleStartGame(PLAYER player, char *opcode, char * buff, char *reserveBuff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) START_E_NOTAUTH, strlen(START_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) START_E_NOTINGAME, strlen(START_E_NOTINGAME), buff);
	else if (player->state == PLAYING) return setResponseAndSend(player, opcode, (char*) START_E_PLAYING, strlen(START_E_PLAYING), buff);
	else if (player->state == READY) return setResponseAndSend(player, opcode, (char*) START_E_NOTHOST, strlen(START_E_NOTHOST), buff);
	else {
		int i;
		GAME game = player->game;
		if (game->host != player->gameIndex) return setResponseAndSend(player, opcode, (char*) START_E_NOTHOST, strlen(START_E_NOTHOST), buff);
		for (i = 0; i < PLAYER_NUM; i++) {
			if (game->players[i] != NULL) {
				if (game->players[i]->state != READY && i != player->gameIndex) {
					return setResponseAndSend(player, opcode, (char*) START_E_NOTALLREADY, strlen(START_E_NOTALLREADY), buff);
				}
			}
		}
		int countTeam = 0;
		for (i = 0; i < TEAM_NUM; i++) {
			for (int j = 0; j < 3; j++) {
				if (game->teams[i]->players[j] != NULL) {
					countTeam++;
					break;
				}
			}
		}
		if (countTeam < 2) return setResponseAndSend(player, opcode, (char*) START_E_ONETEAM, strlen(START_E_ONETEAM), buff);
		memset(buff, 0, BUFF_SIZE);
		// Start game
		game->gameState = ONGOING;
		game->startAt = getTime();
		// Set player state to playing
		for (i = 0; i < PLAYER_NUM; i++) {
			if (game->players[i] != NULL) {
				game->players[i]->state = PLAYING;
			}
		}
		_beginthreadex(NULL, NULL, &timelyUpdate, (void *)game, 0, 0);
		strcpy_s(buff + HEADER_SIZE, BUFF_SIZE, UPDATE_GAME_START);
		setResponse((char*) UPDATE_GAME, buff + HEADER_SIZE, strlen(buff + HEADER_SIZE), buff);
		sendInGameMessageToAllPlayersInGameRoom(game, BUFFLEN, buff, reserveBuff);
		memset(buff + HEADER_SIZE, 0, BUFF_SIZE - 5);
		for (i = 0; i < CASTLE_NUM; i++) {
			sendNewCastleQuestion(game, i, buff, reserveBuff);
		}
		for (i = 0; i < MINE_NUM; i++) {
			for (int j = 0; j < 3; j++) {
				sendNewMineQuestion(game, i, j, buff, reserveBuff);
			}
		}
		return 1;
	}
}

int handleKick(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) KICK_E_NOTAUTH, strlen(KICK_E_NOTAUTH), buff);
	else if (player->state == AUTHORIZED) return setResponseAndSend(player, opcode, (char*) KICK_E_NOTINGAME, strlen(KICK_E_NOTINGAME), buff);
	else if (player->state == PLAYING) return setResponseAndSend(player, opcode, (char*) KICK_E_PLAYING, strlen(KICK_E_NOTINGAME), buff);
	else {
		GAME game = player->game;
		if (player->gameIndex != game->host) return setResponseAndSend(player, opcode, (char*) KICK_E_NOTHOST, strlen(KICK_E_NOTHOST), buff);
		if (strlen(buff) != 1) return setResponseAndSend(player, opcode, (char*) KICK_E_FORMAT, strlen(KICK_E_FORMAT), buff);
		int playerIndex = buff[0] - 48;
		if (playerIndex < 0 || playerIndex > 11) return setResponseAndSend(player, opcode, (char*) KICK_E_FORMAT, strlen(KICK_E_FORMAT), buff);
		if (playerIndex == game->host) return setResponseAndSend(player, opcode, (char*) KICK_E_YOURSELF, strlen(KICK_E_YOURSELF), buff);
		if (game->players[playerIndex] == NULL) return setResponseAndSend(player, opcode, (char*) KICK_E_NOPLAYER, strlen(KICK_E_NOPLAYER), buff);
		PLAYER otherPlayer = game->players[playerIndex];
		game->players[playerIndex] = NULL;
		game->teams[otherPlayer->teamIndex]->players[otherPlayer->placeInTeam] = NULL;
		updatePlayerInfo(otherPlayer, otherPlayer->socket, otherPlayer->IP, otherPlayer->port, 0, 0, 0, 0, otherPlayer->account, AUTHORIZED);
		if (setResponseAndSend(otherPlayer, (char*) KICK, (char*) KICK_SUCCESS, strlen(KICK_SUCCESS), buff) == SOCKET_ERROR) {
			WSACloseEvent(events[otherPlayer->index]);
			closesocket(otherPlayer->socket);
			clearPlayerInfo(otherPlayer, buff);
			if (otherPlayer->index != nEvents - 1) Swap(otherPlayer, players[nEvents - 1], events[otherPlayer->index], events[nEvents - 1]); // Swap last event and socket with the empties
			nEvents--;
		};
		informGameRoomChange(game, playerIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_KICK, buff);
		return 1;
	}
}

/* The handleLogout function handle a logout request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleLogOut(PLAYER player, char *opcode, char *buff) {
	if (player->state == NOT_AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOGOUT_E_NOTAUTH, strlen(LOGOUT_E_NOTAUTH), buff);
	else if (player->state != AUTHORIZED) return setResponseAndSend(player, opcode, (char*) LOGOUT_E_INGAME, strlen(LOGOUT_E_INGAME), buff);
	else {
		map<string, pair<string, int>>::iterator it = accountMap.find(player->account);
		if (it != accountMap.end()) {
			it->second.second = NOT_AUTHORIZED;
		}
		updatePlayerInfo(player, player->socket, player->IP, player->port, 0, 0, 0, 0, 0, NOT_AUTHORIZED);
		return setResponseAndSend(player, opcode, (char*) LOGOUT_SUCCESS, strlen(LOGOUT_SUCCESS), buff);
	}
}

/* The handleAttackCastle function handle a logout request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleAttackCastle(PLAYER player, char *opcode, char *buff, char *reserveBuff) {
	if (player->state != PLAYING) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_NOTPLAYING, strlen(ATK_CST_E_NOTPLAYING), buff);
	else {
		int castleId = 0, questionId = 0, answerId = 0;
		char *firstSharp = strchr(buff, '#');
		if (firstSharp == NULL) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_FORMAT, strlen(ATK_CST_E_FORMAT), buff);
		char *secondSharp = strchr(firstSharp + 1, '#');
		if (secondSharp == NULL) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_FORMAT, strlen(ATK_CST_E_FORMAT), buff);
		int castleIdSize = (int)(firstSharp - buff);
		int questionIdSize = (int)(secondSharp - firstSharp - 1);
		int answerIdSize = strlen(secondSharp + 1);
		if (castleIdSize != 1 || questionIdSize <= 0 || questionId >= 8 || answerIdSize != 1) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_FORMAT, strlen(ATK_CST_E_FORMAT), buff);
		GAME game = player->game;
		TEAM team = game->teams[player->teamIndex];
		firstSharp[0] = 0;
		secondSharp[0] = 0;
		castleId = buff[0] - 48;
		questionId = atoi(firstSharp + 1);
		answerId = secondSharp[1] - 48;
		if (castleId < 0 || castleId > CASTLE_NUM - 1 || answerId < 0 || answerId > 4) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_FORMAT, strlen(ATK_CST_E_FORMAT), buff);
		CASTLE targetCastle = game->castles[castleId];
		if (targetCastle->occupiedBy == player->teamIndex) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_YOURS, strlen(ATK_CST_E_YOURS), buff);
		if (targetCastle->question != questionId) return setResponseAndSend(player, opcode, (char*) ATK_CST_E_TOOLATE, strlen(ATK_CST_E_TOOLATE), buff);
		if (targetCastle->answer != answerId) { // Wrong answer
			if (targetCastle->wall->defense >= team->weapon->attack) { // Lost weapon
				team->weapon->type = NO_WEAPON;
				team->weapon->attack = NO_WEAPON_ATK;
			}
			else { // Reduce attack
				team->weapon->attack -= targetCastle->wall->defense;
			}
			informCastleAttack(game, castleId, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_CST_W, buff, reserveBuff);
		}
		else {
			if (targetCastle->wall->defense > team->weapon->attack) {
				targetCastle->wall->defense -= team->weapon->attack;
				team->weapon->type = NO_WEAPON;
				team->weapon->attack = NO_WEAPON_ATK;
				informCastleAttack(game, castleId, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_CST_R, buff, reserveBuff);
			}
			else if (targetCastle->wall->defense < team->weapon->attack) {
				team->weapon->attack -= targetCastle->wall->defense;
				targetCastle->wall->type = NO_WALL;
				targetCastle->wall->defense = NO_WALL_DEF;
				targetCastle->occupiedBy = player->teamIndex;
				informCastleAttack(game, castleId, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_CST_R, buff, reserveBuff);
			}
			else if (targetCastle->wall->defense == team->weapon->attack) {
				targetCastle->wall->type = NO_WALL;
				targetCastle->wall->defense = NO_WALL_DEF;
				team->weapon->type = NO_WEAPON;
				team->weapon->attack = NO_WEAPON_ATK;
				targetCastle->occupiedBy = player->teamIndex;
				informCastleAttack(game, castleId, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_CST_R, buff, reserveBuff);
			}
		}
	}
	return 1;
}

/* The handleAttackMine function handle a logout request from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleAttackMine(PLAYER player, char *opcode, char *buff, char *reserveBuff) {
	if (player->state != PLAYING) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_NOTPLAYING, strlen(ATK_MINE_E_NOTPLAYING), buff);
	else {
		int mineId = 0, resourceType = 0, questionId = 0, answerId = 0;
		char *firstSharp = strchr(buff, '#');
		if (firstSharp == NULL) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_FORMAT, strlen(ATK_MINE_E_FORMAT), buff);
		char *secondSharp = strchr(firstSharp + 1, '#');
		if (secondSharp == NULL) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_FORMAT, strlen(ATK_MINE_E_FORMAT), buff);
		char *thirdSharp = strchr(secondSharp + 1, '#');
		if (thirdSharp == NULL) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_FORMAT, strlen(ATK_MINE_E_FORMAT), buff);
		firstSharp[0] = 0;
		secondSharp[0] = 0;
		thirdSharp[0] = 0;
		int mineIdSize = (int)(firstSharp - buff);
		int resourceTypeSize = (int)(secondSharp - firstSharp - 1);
		int questionIdSize = (int)(thirdSharp - secondSharp - 1);
		int answerIdSize = strlen(thirdSharp + 1);
		if (mineIdSize != 1 || resourceTypeSize != 1 || questionIdSize <= 0 || questionIdSize > 8 || answerIdSize != 1) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_FORMAT, strlen(ATK_MINE_E_FORMAT), buff);
		mineId = buff[0] - 48;
		resourceType = buff[2] - 48;
		questionId = atoi(secondSharp + 1);
		answerId = thirdSharp[1] - 48;
		if (mineId < 0 || mineId > MINE_NUM - 1 || resourceType < 0 || resourceType > 2 || answerId < 0 || answerId > 4) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_FORMAT, strlen(ATK_MINE_E_FORMAT), buff);
		GAME game = player->game;
		TEAM team = game->teams[player->teamIndex];
		MINE targetMine = game->mines[mineId];
		if (targetMine->question[resourceType] != questionId) return setResponseAndSend(player, opcode, (char*) ATK_MINE_E_TOOLATE, strlen(ATK_MINE_E_TOOLATE), buff);
		if (targetMine->answer[resourceType] != answerId) { // Wrong answer
			informMineAttack(game, mineId, resourceType, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_MINE_W, buff, reserveBuff);
		}
		else { // Take resource and change question
			team->basedResources[resourceType] += targetMine->resources[resourceType];
			targetMine->resources[resourceType] = 0;
			informMineAttack(game, mineId, resourceType, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_ATK_MINE_R, buff, reserveBuff);
		}
	}
	return 1;
}

void setNewWeapon(TEAM team, int weaponId, int weaponAtk, int weaponWood, int weaponStone, int weaponIron) {
	team->weapon->type = weaponId;
	team->weapon->attack = weaponAtk;
	team->basedResources[WOOD] -= weaponWood;
	team->basedResources[STONE] -= weaponStone;
	team->basedResources[IRON] -= weaponIron;
}

/* The handleBuyWeapon function handle a buy weapon from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleBuyWeapon(PLAYER player, char *opcode, char *buff, char *reserveBuff) {
	if (player->state != PLAYING) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_NOTPLAYING, strlen(BUY_WEAPON_E_NOTPLAYING), buff);
	else {
		GAME game = player->game;
		TEAM team = game->teams[player->teamIndex];
		int weaponId = buff[0] - 48;
		if (weaponId <= 0 || weaponId > 3) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_FORMAT, strlen(BUY_WEAPON_E_FORMAT), buff);
		if (weaponId == BALLISTA) { // Ballista
			if (team->weapon->attack >= BALLISTA_ATK) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_WEAKER, strlen(BUY_WEAPON_E_WEAKER), buff);
			if (team->basedResources[WOOD] < BALLISTA_WOOD || team->basedResources[STONE] < BALLISTA_STONE || team->basedResources[IRON] < BALLISTA_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_NOTENOUGH, strlen(BUY_WEAPON_E_FORMAT), buff);
			setNewWeapon(team, BALLISTA, BALLISTA_ATK, BALLISTA_WOOD, BALLISTA_STONE, BALLISTA_IRON);
			informBuyWeapon(game, player->gameIndex, weaponId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WPN, buff, reserveBuff);
		}
		else if (weaponId == CATAPULT) { // Catapult
			if (team->weapon->attack >= CATAPULT_ATK) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_WEAKER, strlen(BUY_WEAPON_E_WEAKER), buff);
			if (team->basedResources[WOOD] < CATAPULT_WOOD || team->basedResources[STONE] < CATAPULT_STONE || team->basedResources[IRON] < CATAPULT_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_NOTENOUGH, strlen(BUY_WEAPON_E_FORMAT), buff);
			setNewWeapon(team, CATAPULT, CATAPULT_ATK, CATAPULT_WOOD, CATAPULT_STONE, CATAPULT_IRON);
			informBuyWeapon(game, player->gameIndex, weaponId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WPN, buff, reserveBuff);
		}
		else if (weaponId == CANNON) { // Cannon
			if (team->weapon->attack >= CANNON_ATK) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_WEAKER, strlen(BUY_WEAPON_E_WEAKER), buff);
			if (team->basedResources[WOOD] < CANNON_WOOD || team->basedResources[STONE] < CANNON_STONE || team->basedResources[IRON] < CANNON_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WEAPON_E_NOTENOUGH, strlen(BUY_WEAPON_E_FORMAT), buff);
			setNewWeapon(team, CANNON, CANNON_ATK, CANNON_WOOD, CANNON_STONE, CANNON_IRON);
			informBuyWeapon(game, player->gameIndex, weaponId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WPN, buff, reserveBuff);
		}
	}
	return 1;
}

void setNewWall(TEAM team, CASTLE castle, int wallId, int wallDefense, int wallWood, int wallStone, int wallIron) {
	castle->wall->type = wallId;
	castle->wall->defense = wallDefense;
	team->basedResources[WOOD] -= wallWood;
	team->basedResources[STONE] -= wallStone;
	team->basedResources[IRON] -= wallIron;
}

/* The handleBuyWall function handle a buy wall from player
* @param	player		[IN/OUT]	Request player
* @param	opcode		[IN/OUT]	Operation code
* @param	buff		[IN/OUT]	Buffer to store game info
* @return	1
*/
int handleBuyWall(PLAYER player, char *opcode, char *buff, char *reserveBuff) {
	if (player->state != PLAYING) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_NOTPLAYING, strlen(BUY_WALL_E_NOTPLAYING), buff);
	else {
		GAME game = player->game;
		TEAM team = game->teams[player->teamIndex];
		int castleId = 0, wallId = 0;
		if (strlen(buff) != 3) setResponseAndSend(player, opcode, (char*) BUY_WALL_E_FORMAT, strlen(BUY_WALL_E_FORMAT), buff);
		castleId = buff[0] - 48;
		wallId = buff[2] - 48;
		if (castleId < 0 || castleId > CASTLE_NUM - 1 || wallId <= 0 || wallId > 4) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_FORMAT, strlen(BUY_WALL_E_FORMAT), buff);
		CASTLE castle = game->castles[castleId];
		if (player->teamIndex != castle->occupiedBy) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_GONE, strlen(BUY_WALL_E_GONE), buff);
		if (wallId == FENCE) {
			if (castle->wall->defense >= FENCE_DEF) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_WEAKER, strlen(BUY_WALL_E_WEAKER), buff);
			if (team->basedResources[WOOD] < FENCE_WOOD || team->basedResources[STONE] < FENCE_STONE || team->basedResources[IRON] < FENCE_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_NOTENOUGH, strlen(BUY_WALL_E_NOTENOUGH), buff);
			setNewWall(team, castle, FENCE, FENCE_DEF, FENCE_WOOD, FENCE_STONE, FENCE_IRON);
			informBuyWall(game, player->gameIndex, castleId, wallId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WALL, buff, reserveBuff);
		}
		else if (wallId == WOOD_WALL) {
			if (castle->wall->defense >= WOOD_WALL_DEF) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_WEAKER, strlen(BUY_WALL_E_WEAKER), buff);
			if (team->basedResources[WOOD] < WOOD_WALL_WOOD || team->basedResources[STONE] < WOOD_WALL_STONE || team->basedResources[IRON] < WOOD_WALL_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_NOTENOUGH, strlen(BUY_WALL_E_NOTENOUGH), buff);
			setNewWall(team, castle, WOOD_WALL, WOOD_WALL_DEF, WOOD_WALL_WOOD, WOOD_WALL_STONE, WOOD_WALL_IRON);
			informBuyWall(game, player->gameIndex, castleId, wallId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WALL, buff, reserveBuff);
		}
		else if (wallId == STONE_WALL) {
			if (castle->wall->defense >= STONE_WALL_DEF) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_WEAKER, strlen(BUY_WALL_E_WEAKER), buff);
			if (team->basedResources[WOOD] < STONE_WALL_WOOD || team->basedResources[STONE] < STONE_WALL_STONE || team->basedResources[IRON] < STONE_WALL_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_NOTENOUGH, strlen(BUY_WALL_E_NOTENOUGH), buff);
			setNewWall(team, castle, STONE_WALL, STONE_WALL_DEF, STONE_WALL_WOOD, STONE_WALL_STONE, STONE_WALL_IRON);
			informBuyWall(game, player->gameIndex, castleId, wallId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WALL, buff, reserveBuff);
		}
		else if (wallId == LEGEND_WALL) {
			if (castle->wall->defense >= LEGEND_WALL_DEF) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_WEAKER, strlen(BUY_WALL_E_WEAKER), buff);
			if (team->basedResources[WOOD] < LEGEND_WALL_WOOD || team->basedResources[STONE] < LEGEND_WALL_STONE || team->basedResources[IRON] < LEGEND_WALL_IRON) return setResponseAndSend(player, opcode, (char*) BUY_WALL_E_NOTENOUGH, strlen(BUY_WALL_E_NOTENOUGH), buff);
			setNewWall(team, castle, LEGEND_WALL, LEGEND_WALL_DEF, LEGEND_WALL_WOOD, LEGEND_WALL_STONE, LEGEND_WALL_IRON);
			informBuyWall(game, player->gameIndex, castleId, wallId, (char*) UPDATE_GAME, (char*) UPDATE_GAME_BUY_WALL, buff, reserveBuff);
		}
		return 1;
	}
}

int handleCheat(PLAYER player, char *opcode, char *buff, char * reserveBuff) {
	if (player->state != PLAYING) return setResponseAndSend(player, opcode, (char*) CHEAT_E_NOTPLAYING, strlen(CHEAT_E_NOTPLAYING), buff);
	else {
		GAME game = player->game;
		TEAM team = game->teams[player->teamIndex];
		for (int i = 0; i < RESOURCE_NUM; i++) {
			if (team->basedResources[i] + CHEAT_AMOUNT > MAX_AMOUNT) return setResponseAndSend(player, opcode, (char*) CHEAT_E_GREEDY, strlen(CHEAT_E_GREEDY), buff);
		}
		for (int i = 0; i < RESOURCE_NUM; i++) team->basedResources[i] += CHEAT_AMOUNT;
		informCheat(game, player->gameIndex, (char*) UPDATE_GAME, (char*) UPDATE_GAME_CHEAT, buff, reserveBuff);
	}
	return 1;
}

/* The updatePlayerInfo function clear a player info
* @param	player			[IN/OUT]	Player
* @return	nothing
*/
void clearPlayerInfo(PLAYER player, char *reserveBuff) {
	// Remove player from game and team
	if (player->game != NULL) {
		GAME game = player->game;
		int i;
		game->players[player->gameIndex] = NULL;
		game->teams[player->teamIndex]->players[player->placeInTeam] = NULL;
		for (i = 0; i < PLAYER_NUM; i++) {
			if (game->players[i] != NULL) {
				break;
			}
		}
		if (i == PLAYER_NUM) emptyGame(game);
		else {
			if (player->gameIndex == game->host) game->host = i;
			memset(reserveBuff, 0, BUFF_SIZE);
			informGameRoomChange(game, player->gameIndex, (char*) UPDATE_LOBBY, (char*) UPDATE_LOBBY_QUIT, reserveBuff);
		}
	}
	map<string, pair<string, int>>::iterator it = accountMap.find(player->account);
	if (it != accountMap.end()) it->second.second = NOT_AUTHORIZED;
	updatePlayerInfo(player, 0, 0, 0, 0, 0, 0, 0, 0, NOT_AUTHORIZED);
	return;
}