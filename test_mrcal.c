#include "mrcal.h"
#include <stdio.h>

int main() {
    
    mrcal_problem_selections_t selections;
    selections.do_optimize_calobject_warp = 1;
    int states = mrcal_num_states_calobject_warp(selections, 1);

    printf("hello! states=%i\n", states);

    return 0;
}
