#pragma once

#include "auth.h"
#include "lobby.h"
#include "player.h"
#include "question.h"

struct Response {
	char result_code[RESULT_CODE_SIZE + 1];
};

struct Auth: public Response {
};

struct Create_lobby : public Response  {
	int id;
};

struct Get_lobby : public Response {
	Lobby lobbies[MAX_LOBBY];
	int size;
};

struct Join_lobby : public Response  {
	unsigned long long id;
	int player_id;
	int team_number;
	int team_id;
};

struct Ready : public Response {
};

struct Unready : public Response{
};

struct Change_team : public Response {
};


struct Quit_lobby : public Response  {
};

struct Start_game : public Response  {

};

struct Update_lobby : public Response  {
	unsigned long long game_id;
	int team_number;
	int request_player_id;
	int team_players[MAX_NUM_PLAYER];
	Player players[MAX_NUM_PLAYER];

	//Extra info
	int player_number;
};


struct Buy_wall : public Response{

};

struct Buy_weapon : public Response {

};

struct Attack_castle : public Response {

};

struct Attack_mine : public Response {

};

struct Update_game : public Response  {
};

struct Update_question : public Update_game {
	int question_id;
	char question[QUESTION_LENGTH + 1];
	char answer1[ANSWER_LENGTH + 1];
	char answer2[ANSWER_LENGTH + 1];
	char answer3[ANSWER_LENGTH + 1];
	char answer4[ANSWER_LENGTH + 1];
};

struct Update_castle_ques : public Update_question {
	int castle_id;
};

struct Update_mine_ques : public Update_question {
	int mine_id;
};

struct Update_castle_attack : public Update_question {
	int player_id;
	int team_id;
	int castle_id;
	int wall_type_id;
	int wall_def;
	int weapon_type_id;
	int weapon_atk;
};

struct Update_mine_attack : public Update_question {
	int player_id;
	int team_id;
	int mine_id;
};

struct Update_buy_weapon : public Update_game {
	int player_id;
	int team_id;
	int weapon_type_id;
};

struct Update_buy_wall : public Update_game {
	int player_id;
	int team_id;
	int wall_type_id;
};



struct Update_timely{
	// Mine info
	int wood_mine[MAX_MINE_OF_GAME];
	int stone_mine[MAX_MINE_OF_GAME];
	int iron_mine[MAX_MINE_OF_GAME];

};