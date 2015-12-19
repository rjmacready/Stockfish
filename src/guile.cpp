
#include "guile.h"

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"


SCM display;
SCM evaluate;
SCM get_multipv;
SCM pick_best;

// Position

SCM get_side_to_move(SCM pos) {
  Position *rpos = (Position*)scm_to_pointer(pos);
  return scm_from_int(rpos->side_to_move());
}

SCM get_fen(SCM pos) {
  Position *rpos = (Position*)scm_to_pointer(pos);
  return scm_from_locale_string(rpos->fen().c_str());
}

SCM get_pinned_pieces(SCM pos, SCM color) {
  Position *rpos = (Position*)scm_to_pointer(pos);
  Color rcolor = (Color)scm_to_int(color);
  return scm_from_uint64(rpos->pinned_pieces(rcolor));
}

// ... other stuff

// Root Moves

SCM get_root_moves() {
  return scm_from_pointer(&(Threads.main()->rootMoves), NULL);
  //return SCM_UNDEFINED;
}

SCM get_root_moves_len(SCM movesList) {
  Search::RootMoveVector *rptr = (Search::RootMoveVector*) scm_to_pointer(movesList);
  return scm_from_int(rptr->size());
  //return SCM_UNDEFINED;
}

SCM get_root_move_at(SCM movesList, SCM idx) {
  Search::RootMoveVector *rptr = (Search::RootMoveVector*) scm_to_pointer(movesList);
  int ridx = scm_to_int(idx);
  
  return scm_from_pointer(&((*rptr)[ ridx ]), NULL);
} 

SCM get_score(SCM rootMove) {
  if(scm_is_false(rootMove))
    return SCM_BOOL_F;
  
  Search::RootMove* rroot = (Search::RootMove*) scm_to_pointer(rootMove);
  return scm_from_int(rroot->score);
}

SCM get_pv(SCM rootMove) {
  if(scm_is_false(rootMove))
    return SCM_BOOL_F;
  
  Search::RootMove* rroot = (Search::RootMove*) scm_to_pointer(rootMove);
  return scm_from_pointer(&(rroot->pv), NULL);
}

SCM get_pv_len(SCM pv) {
  if(scm_is_false(pv))
    return scm_from_int(0);
  
  return scm_from_int(
		      (*((std::vector<Move>*) scm_to_pointer(pv))).size()
		      );
}

SCM get_pv_entry_at(SCM pv, SCM idx) {
  if(scm_is_false(pv))
    return SCM_BOOL_F;
  
  //  std::vector<Move> rvec = *((std::vector<Move>*) scm_to_pointer(pv));
  int ridx = scm_to_int(idx);
  //  Move m = rvec[ridx];
  //  printf("get_pv_entry_at: %s\n", UCI::move(m, false).c_str());

  std::vector<Move>* moves = (std::vector<Move>*) scm_to_pointer(pv);
  //printf("get_pv_entry_at: %x %d\n", moves, idx);
  //printf("size: %d\n", moves->size());
  
  return scm_from_pointer(&(
			    (*moves)[ ridx ]), NULL);
}

SCM to_str(SCM pvEntry) {
  //  Move mv = *((Move*) scm_to_pointer(pvEntry));
  return scm_from_locale_string(UCI::move(
					  *((Move*) scm_to_pointer(pvEntry))
					  , false).c_str());
}

// Move
SCM from_sq(SCM move) {
  return SCM_UNDEFINED;
}

SCM to_sq(SCM move) {
  return SCM_UNDEFINED;
}

// ---

void* init_guile(void* data)
{
  scm_gc();
  
  scm_c_primitive_load("/home/user/Stockfish/src/userscripts.scm");

  display = scm_c_public_lookup("guile", "display");
  // , 7, NULL, SCM_FAILED_CONVERSION_ERROR
  scm_call_1(scm_variable_ref(display), scm_from_locale_string("from C\n"));

  scm_c_define_gsubr("get-root-moves", 0, 0, 0, (void*)&get_root_moves);
  scm_c_define_gsubr("get-root-moves-len", 1, 0, 0, (void*)&get_root_moves_len);
  scm_c_define_gsubr("side-to-move", 1, 0, 0, (void*)&get_side_to_move);
  scm_c_define_gsubr("get-fen", 1, 0, 0, (void*)&get_fen);
  scm_c_define_gsubr("get-root-move-at", 2, 0, 0, (void*)&get_root_move_at);
  scm_c_define_gsubr("get-score", 1, 0, 0, (void*)&get_score);
  scm_c_define_gsubr("get-pv", 1, 0, 0, (void*)&get_pv);
  scm_c_define_gsubr("get-pv-len", 1, 0, 0, (void*)&get_pv_len);
  scm_c_define_gsubr("get-pv-entry-at", 2, 0, 0, (void*)&get_pv_entry_at);
  scm_c_define_gsubr("to-str", 1, 0, 0, (void*)&to_str);

  evaluate = scm_c_public_lookup("userscripts", "evaluate");
  get_multipv = scm_c_public_lookup("userscripts", "get-multipv");
  pick_best = scm_c_public_lookup("userscripts", "pick-best");
  
  // scm_call_1(scm_variable_ref(display), scm_variable_ref(evaluate));
  
  scm_c_primitive_load("/home/user/Stockfish/src/main.scm");

  //  printf("thread: %d\n", std::thread::id());
  printf("init for thread: %d %d\n", std::thread::id(), std::this_thread::get_id());

  //scm_call_0(scm_variable_ref(evaluate));
  //scm_call_0(scm_variable_ref(evaluate));
  //scm_call_0(scm_variable_ref(evaluate));
    
  return NULL;
}

size_t guile_get_multipv() {
  if(get_multipv != NULL) {
    return (size_t)scm_to_int(scm_call_0(scm_variable_ref(get_multipv)));
  }
  return 0;
}

Value guile_evaluate(const Position& pos, Value v)
{
  
  if(evaluate != NULL) {  
    //SCM str = scm_from_stringn("from C\n", 7, NULL, SCM_FAILED_CONVERSION_ERROR);
    //SCM display = scm_c_public_lookup("", "display"); // guile

    //printf("thread: %d %d\n", std::thread::id(), std::this_thread::get_id());

    //evaluate = scm_c_public_lookup("userscripts", "evaluate");
    //scm_call_1(scm_variable_ref(display), scm_variable_ref(evaluate));

    //  printf("was %d\n", v);
  
    SCM r = scm_call_2(scm_variable_ref(evaluate),
		       scm_from_pointer((void*)&pos, NULL),
		       scm_from_int(v));

    //  printf("now is %d\n", (Value)scm_to_int(r));
  
    return (Value)scm_to_int(r);
  }

  return v;
}

void guile_pick_best(size_t multiPV)
{
  if(pick_best != NULL) {
    scm_call_1(scm_variable_ref(pick_best), scm_from_int(multiPV));
  }
}
