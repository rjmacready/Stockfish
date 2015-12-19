#ifndef GUILE_H_INCLUDED
#define GUILE_H_INCLUDED

#define WITH_GUILE

#include <libguile.h>
#include <thread>

#include "evaluate.h"
//#include "search.h"
//#include "movepick.h"
#include "types.h"
#include "position.h"
//#include "thread.h"

//extern "C" SCM get_side_to_move(SCM pos);

void* init_guile(void* data);

void guile_pick_best(size_t multiPV);
size_t guile_get_multipv();
Value guile_evaluate(const Position& pos, Value v);

#endif
