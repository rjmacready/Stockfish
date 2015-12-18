
#include "guile.h"


SCM display;
SCM evaluate;


SCM get_side_to_move(SCM pos)
{
  Position *rpos = (Position*)scm_to_pointer(pos);

  return scm_from_int(rpos->side_to_move());
}

void* init_guile(void* data)
{
  scm_gc();
  
  scm_c_primitive_load("/home/user/Stockfish/src/userscripts.scm");

  display = scm_c_public_lookup("guile", "display");
  scm_call_1(scm_variable_ref(display), scm_from_stringn("from C\n", 7, NULL, SCM_FAILED_CONVERSION_ERROR));

  scm_c_define_gsubr("side-to-move", 1, 0, 0, (void*)&get_side_to_move);
  
  evaluate = scm_c_public_lookup("userscripts", "evaluate");
  //evaluate = scm_c_public_lookup("guile-user", "evaluate");
  
  scm_call_1(scm_variable_ref(display), scm_variable_ref(evaluate));
  
  scm_c_primitive_load("/home/user/Stockfish/src/main.scm");

  //  printf("thread: %d\n", std::thread::id());
  printf("init for thread: %d %d\n", std::thread::id(), std::this_thread::get_id());

  //scm_call_0(scm_variable_ref(evaluate));
  //scm_call_0(scm_variable_ref(evaluate));
  //scm_call_0(scm_variable_ref(evaluate));
    
  return NULL;
}

Value guile_evaluate(const Position& pos, Value v)
{
  
  assert(evaluate != NULL);
  
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
