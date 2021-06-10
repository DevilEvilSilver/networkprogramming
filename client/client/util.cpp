#include "stdafx.h"
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "constant.h"
#include "lobby.h"
#include "player.h"


void pack(char* code, char* payload, char* mess) {

	// Cast payload length -> string
	char payload_len[PAYLOAD_LEN_SIZE + 1];
	int len = strlen(payload);
	int first_byte = len / 256;
	int second_byte = len % 256;
	payload_len[0] = first_byte;
	payload_len[1] = second_byte;

	// Pack as 1 request
	strcat_s(mess, BUFF_SIZE, code);
	strcat_s(mess, BUFF_SIZE, payload_len);
	strcat_s(mess, BUFF_SIZE, payload);

}

void auth_payload(char* username, char* password, char* payload) {
	strcat_s(payload, PAYLOAD_SIZE + 1, username);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, password);
}

void join_lobby_payload(char* game_id, char* team_id, char* payload) {
	strcat_s(payload, PAYLOAD_SIZE + 1, game_id);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, team_id);
}

void attack_castle_payload(char* castle_id, char* question_id, char* answer_id, char* payload) {
	strcat_s(payload, PAYLOAD_SIZE + 1, castle_id);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, question_id);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, answer_id);
}

void attack_mine_payload(char* mine_id, char* type, char* question_id, char* answer_id, char* payload) {
	strcat_s(payload, PAYLOAD_SIZE + 1, mine_id);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, type);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, question_id);
	strcat_s(payload, PAYLOAD_SIZE + 1, DELIM_REQ_RES);
	strcat_s(payload, PAYLOAD_SIZE + 1, answer_id);
}

Auth auth_data(char* payload) {
	Auth result;
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, payload);
	return result;
}

Create_lobby create_lobby_data(char* payload) {
	Create_lobby result;
	char *next_token;
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	result.id = atoi(strtok_s(NULL, DELIM_REQ_RES, &next_token));
	return result;
}

Get_lobby get_lobby_data(char* payload) {
	Get_lobby result;
	char *next_token;

	// Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	int id;
	int team_number;
	char team_players[TEAM_PLAYER_NUM_STR];

	// Get list lobby 
	int i = 0;
	while (token) {
		// Get lobby id
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		id = atoi(token);

		// Get team number
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		team_number = atoi(token);

		// Get team player
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(team_players, TEAM_PLAYER_NUM_STR, token);

		Lobby lobby{ id, team_number };
		resolve_team_player_str(team_players, lobby.team_number, lobby.team_players);
		result.lobbies[i] = lobby;

		i++;
	}
	result.size = i;

	return result;
}

Join_lobby join_lobby_data(char* payload) {
	Join_lobby result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);

	// Get game id of this client
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	if (!token) {
		return result;
	}
	result.id = atoi(token);

	// Get team number
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	result.team_number = atoi(token);

	// Get team memeber string
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	int team_players[MAX_NUM_PLAYER];
	resolve_team_player_str(token, result.team_number, team_players);

	int player_ingame_id;
	char player_name[USERNAME_LEN];
	int player_state;
	int i = 0;

	// Get player detail info
	while (token) {
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		player_ingame_id = atoi(token);
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(player_name, USERNAME_LEN, token);
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		player_state = atoi(token);
		result.players[i] = Player{ player_ingame_id, player_name, result.id, team_players[player_ingame_id], player_state };

		i++;
	}
	result.player_number = i;

	return result;
}


Change_team change_team_data(char* payload) {
	Change_team result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	return result;
}

Ready ready_data(char* payload) {
	Ready result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	return result;
}

Unready unready_data(char* payload) {
	Unready result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	return result;
}

Quit_lobby quit_lobby_data(char* payload) {
	Quit_lobby result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	return result;
}

Start_game start_game_data(char* payload) {
	Start_game result;
	char *next_token;

	//Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);
	return result;
}

void resolve_team_player_str(char* string, int team_number, int* member_team) {
	for (int i = 0; i < team_number * MAX_PLAYER_OF_TEAM; i++) {
		if (string[i] == 'x') {
			member_team[i] = -1;
		}
		else {
			member_team[i] = string[i] - '0';
		}
	}
}


Update_lobby update_lobby_data(char* payload) {
	Update_lobby result;
	char* next_token;
	// Get result code
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	strcpy_s(result.result_code, RESULT_CODE_SIZE + 1, token);

	// Get result code
	token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	result.game_id = atoi(token);

	// Get team number
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	result.team_number = atoi(token);

	// Get request player id
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	result.request_player_id = atoi(token);

	// Get team player str
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	resolve_team_player_str(token, result.team_number, result.team_players);

	int player_ingame_id;
	char player_name[USERNAME_LEN];
	int player_state;
	int i = 0;

	// Get player detail info
	while (token) {
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		player_ingame_id = atoi(token);
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(player_name, USERNAME_LEN, token);
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		player_state = atoi(token);
		result.players[i] = Player{ player_ingame_id, player_name, result.game_id, result.team_players[player_ingame_id], player_state };
		i++;
	}

	result.player_number = i;

	return result;
}


Castle_question castle_question_data(char* payload) {
	Castle_question result;

	char* next_token;
	// Get game id
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	result.id = atoi(token);

	// Get castle id
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	result.castle_id = atoi(token);

	int i = 0;
	int question_id;
	char question[QUESTION_LENGTH];
	char answer1[ANSWER_LENGTH];
	char answer2[ANSWER_LENGTH];
	char answer3[ANSWER_LENGTH];
	char answer4[ANSWER_LENGTH];

	while (token) {
		// Get question id
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		question_id = atoi(token);
		// Get question
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(question, QUESTION_LENGTH + 1, token);

		// Get answer
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer1, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer2, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer3, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer4, ANSWER_LENGTH + 1, token);

		result.questions[i] = Question(question_id, question, answer1, answer2, answer3, answer4);
		i++;
	}

	return result;
}

Mine_question mine_question_data(char* payload) {
	Mine_question result;

	char* next_token;
	// Get game id
	char* token = strtok_s(payload, DELIM_REQ_RES, &next_token);
	result.id = atoi(token);

	// Get mine id
	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
	result.mine_id = atoi(token);

	token = strtok_s(NULL, DELIM_REQ_RES, &next_token);

	// Get mine type
	result.type = atoi(token);

	int i = 0;
	int question_id;
	char question[QUESTION_LENGTH];
	char answer1[ANSWER_LENGTH];
	char answer2[ANSWER_LENGTH];
	char answer3[ANSWER_LENGTH];
	char answer4[ANSWER_LENGTH];

	while (token) {
		// Get question id
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		question_id = atoi(token);
		// Get question
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(question, QUESTION_LENGTH + 1, token);

		// Get answer
		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer1, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer2, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer3, ANSWER_LENGTH + 1, token);

		token = strtok_s(NULL, DELIM_REQ_RES, &next_token);
		strcpy_s(answer4, ANSWER_LENGTH + 1, token);

		result.questions[i] = Question(question_id, question, answer1, answer2, answer3, answer4);
		i++;
	}

	return result;
}