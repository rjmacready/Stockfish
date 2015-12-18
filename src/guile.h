#ifndef GUILE_H_INCLUDED
#define GUILE_H_INCLUDED

#define WITH_GUILE

#include <libguile.h>
#include <thread>

#include "types.h"
#include "position.h"

//extern "C" SCM get_side_to_move(SCM pos);

void* init_guile(void* data);

Value guile_evaluate(const Position& pos, Value v);

#endif
