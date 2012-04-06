
#include <stdio.h>
#include <set>
#include "gm_backend_gps.h"
#include "gm_error.h"
#include "gm_code_writer.h"
#include "gm_frontend.h"
#include "gm_transform_helper.h"
#include "gm_typecheck.h"
#include "gps_syminfo.h"

//---------------------------------------------------------------------
// Traverse AST per each BB
//  - figure out how each symbol is used.
//     (as LHS, as RHS, as REDUCE targe)
//     (in MASTER, in RECEIVER, in VERTEX)
//---------------------------------------------------------------------
class gps_merge_symbol_usage_t : public gps_apply_bb_ast  
{
    static bool const IS_SCALAR = true;
    static bool const IS_FIELD  = false;

    public:
        gps_merge_symbol_usage_t(gm_gps_beinfo* i) {
            set_for_sent(true);
            set_for_expr(true);
            set_separate_post_apply(true); 
            foreach_depth = 0;
            beinfo = i;
            is_random_write_target = false;
            random_write_target = NULL;
        }
    
    virtual bool apply(ast_sent* s) 
    {
        // only need to look at assign statement (for LHS)
        // (RHS usages will be gathered in apply(expr)
        if (s->get_nodetype() == AST_ASSIGN)
        {
            ast_assign * a = (ast_assign*) s;

            int context = get_current_context();
            random_write_target_sb = s->find_info_ptr(GPS_FLAG_SENT_SYMBOL_SB);

            // check if random write
            is_random_write_target = (random_write_target_sb != NULL);
            if (is_random_write_target) {
                assert(!a->is_target_scalar());
                random_write_target = a->get_lhs_field()->get_first()->getSymInfo();
            }

            if (foreach_depth > 1) {
                if (context == GPS_CONTEXT_MASTER) // inner loop
                    return true;
            }

            // save lhs usage
            ast_id* target =  (a->is_target_scalar()) ? a->get_lhs_scala() : a->get_lhs_field()->get_second();
            int is_scalar = (a->is_target_scalar()) ? IS_SCALAR : IS_FIELD;
            int lhs_reduce = a->is_reduce_assign() ? GPS_SYM_USED_AS_REDUCE : GPS_SYM_USED_AS_LHS;
            int r_type = a->is_reduce_assign() ? a->get_reduce_type() : GMREDUCE_NULL;
            update_access_information(target, is_scalar, lhs_reduce, context, r_type);
        }

        if (s->get_nodetype() == AST_FOREACH) 
        {
            ast_foreach* fe = (ast_foreach*) s;
            foreach_depth++;
            if (foreach_depth == 2) {
                in_loop = fe;
            }
            else if (foreach_depth == 1) {
                out_loop = fe;
                out_iterator = fe->get_iterator()->getSymInfo();
            }
            else {
                assert(false);
            }
        }
    }

    virtual bool apply2(ast_sent * s) {
        if (s->get_nodetype() == AST_FOREACH) 
        {
            foreach_depth--;
        }
    }

    virtual bool apply(ast_expr* e) 
    {
        if (!e->is_id() && !e->is_field())
            return true;

        //----------------------------------------------------------
        // following symbols have to be sent over network
        //----------------------------------------------------------
        // Foreach(s: G.Nodes) {
        //   Int x = ... ;
        //   Foreach(t: s.OutNbrs) {
        //       y = s.A + t.B;   // A;   // accessed through s
        //       t.Y += x + y;    // x;   // scoped in s
        // } }
        //----------------------------------------------------------
        int context = get_current_context();
        int used_type = GPS_SYM_USED_AS_RHS;
        bool is_id = e->is_id();
        int sc_type = is_id: IS_SCALAR : IS_FIELD;
        ast_id* tg = is_id ? e->get_id() : e->get_field()->get_second();
        gm_symtab_entry* drv = is_id ? NULL : e->get_field()->get_first()->getSymInfo();
        bool ignored_symbol = false;

        // comm_symbol
        bool comm_symbol = false;
        if (is_random_write_target) {
            if (is_id) {
                if (tg->getSymInfo() != random_write_target)
                    comm_symbol = true;
            else {
                if (drv != random_write_target) {
                    comm_symbol = true;
                }
            }
        }
        else if (context == GPS_CONTEXT_VERTEX) {
            if (foreach_depth > 1) {
                if (is_id) { 
                    if (gps_get_global_syminfo(tg)->is_scoped_outer() || 
                        tg->getSymInfo() == outiterator)
                        comm_symbol = true;
                }
                else {

                }
            }
        }


            }
        }

        if (!ignored_symbol) {
            update_access_information(tg, sc_type, used_type, context);  
        }

        if (comm_symbol) {
            if (is_random_write_target) {
                beinfo->add_communication_symbol_random_write(
                        random_write_target_sb, random_write_target, 
                        tg->getSymInfo());
            }
            else {
                beinfo->add_communication_symbol_nested(in_loop, tg->getSymInfo());
            }
            tg->getSymInfo()->add_info_bool(GPS_FLAG_SENT_SYMBOL, true);
        }

            assert(false);}

        // RHS
        if (e->is_id()) {
            i = e->get_id();

            if (i->getSymInfo()->getType()->is_node_iterator())
            {
                 // matters only if this is an outer node iterator inside 
                if ((foreach_depth == 2) && (i->getSymInfo() == out_iterator)) {

                }
            }
            else 
            
            
            if (!i->getSymInfo()->getType()->is_node_iterator())
            {
                update_access_information(i, IS_SCALAR, GPS_SYM_USED_AS_RHS, context);  

                // check if this symbol should be sent over network
                if (foreach_depth == 2) {
                    if (gps_get_global_syminfo(i)->is_scoped_outer()) {
                        beinfo->add_communication_symbol_nested(in_loop, i->getSymInfo());
                    }
                }
            }
        } 
        else if (e->is_field()) {
            ast_id* prop = e->get_field()->get_second();
            update_access_information(prop, IS_FIELD, GPS_SYM_USED_AS_RHS, context);

            // check if this symbol should be sent over network
            if (foreach_depth == 2) {
                gm_symtab_entry* driver_sym = e->get_field()->get_first()->getSymInfo();
                if (driver_sym == out_iterator) { 
                    beinfo->add_communication_symbol_nested(in_loop, prop->getSymInfo());
                }
            }
        }
    }
    protected:

        int get_current_context() {
            int context;
            if (!get_curr_BB()->is_vertex()) {
                // master context
                context = GPS_CONTEXT_MASTER;
            }
            else {
                // sender/recevier
                if (is_under_receiver_traverse()) 
                    context = GPS_CONTEXT_RECEIVER;
                else
                    context = GPS_CONTEXT_VERTEX;
            }

            return context;
        }

        void update_access_information(ast_id *i, 
                bool is_scalar, int usage, int context, int r_type = GMREDUCE_NULL)
        {
            // update global information
            gps_syminfo* syminfo = 
                get_or_create_global_syminfo(i, is_scalar);

            // update global information
            syminfo->add_usage_in_BB(
                    get_curr_BB()->get_id(), 
                    usage,
                    context,
                    r_type);

            // update local information
            syminfo = 
                get_or_create_local_syminfo(i, is_scalar);

            syminfo->add_usage_in_BB(
                    get_curr_BB()->get_id(), 
                    usage,
                    context,
                    r_type);

            printf("Add usage : %s for BB : %d, context: %d\n", 
                    i->get_genname(),
                    get_curr_BB()->get_id(), context);
        }

        gps_syminfo* get_or_create_global_syminfo(ast_id *i, bool is_scalar)
        {
            gm_symtab_entry* sym = i->getSymInfo(); 

            ast_extra_info* info = sym->find_info(TAG_BB_USAGE);
            gps_syminfo* syminfo;
            if (info == NULL)  {
                syminfo = new gps_syminfo(is_scalar);
                sym->add_info(TAG_BB_USAGE, syminfo);
            } else {
                syminfo = (gps_syminfo*) info;
            }
            return syminfo;
        }

        gps_syminfo* get_or_create_local_syminfo(ast_id *i, bool is_scalar)
        {
            gm_symtab_entry* sym = i->getSymInfo(); 

            // find info from BB-local map
            gps_syminfo* syminfo =
                get_curr_BB()-> find_symbol_info(sym);
            if (syminfo == NULL)  {
                syminfo = new gps_syminfo(is_scalar);
                get_curr_BB()->add_symbol_info(sym, syminfo);
            }
            return syminfo;
        }


        int foreach_depth;

        gm_symtab_entry* out_iterator;
        ast_foreach*     in_loop;
        ast_foreach*     out_loop;
        gm_gps_beinfo*   beinfo;

        bool is_random_write_target;
        gm_symtab_entry* random_write_target;
        ast_sentblock* random_write_target_sb;
};


void gm_gps_opt_analyze_symbol_usage::process(ast_procdef* p)
{
    
    gm_gps_beinfo * beinfo = 
        (gm_gps_beinfo *) FE.get_backend_info(p);
    gm_gps_basic_block* entry_BB = beinfo->get_entry_basic_block();
    assert(p!= NULL);
    assert(entry_BB!=NULL);

    // traverse BB
    gps_merge_symbol_usage_t T(beinfo);
    gps_bb_traverse_ast(entry_BB, &T, true, true);

    set_okay(true);
}

