
#include "mpconfigport.h"
#include "stdint.h"
#include "py/mpconfig.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "modmachine.h"


STATIC const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umachine) },
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&machine_pin_type) },
};

STATIC MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

const mp_obj_module_t mp_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&machine_module_globals,
};


