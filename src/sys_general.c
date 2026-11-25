#include "sys_general.h"
#include "gabe_reg.h"
#include "exp_reg.h"
#include "interrupt.h"

/*
 * Fill out a s_MODEL_info structure with the information about the current system
 *
 * Inputs:
 * info = pointer to a s_MODEL_info structure to fill out
 */
void sys_get_information(p_sys_info info) {
    unsigned long gabe_id = 0;
    unsigned short gabe_id_Genx = 0;    // Needs to be a 16bits for access to the CPLD registers. (0xFEC005xx Region)
    unsigned short genx_model_id = 0;
    unsigned short clock_speed = 0;
    unsigned short cpu = 0;
    unsigned short machine_id = 0;

    info->mcp_version = VER_MAJOR;
    info->mcp_rev = VER_MINOR;
    info->mcp_build = VER_BUILD;

    info->has_floppy = 0;

    info->system_ram_size = 0xC00000;

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
//{ CHIP_SUBVERSION[15:0], CPU_ID ,4'b0000, CPU_SPEED_ID , MACHINE_ID };
/* 
localparam 		CPU_SPEED_ID 	= 4'b0010,	// 2 = 25Mhz
				MACHINE_ID   	= 4'b1011,	// 11 = A2560K
				CPU_ID       	= 4'b0110;  // 6 = MC68040V
*/
    gabe_id = *GABE_SUBVER_ID;              //
    clock_speed = (gabe_id & 0xF0) >> 4;    // 
    cpu = (gabe_id & 0xF000) >> 12;         //

#if ( MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X )
    gabe_id_Genx = *GABE_SUB_MODEL_ID;  // diffent register inside the CPLD - not the FPGA - The FPGA will report a reserved code 0101
    machine_id = (gabe_id_Genx & 0x000F);
#else 
    machine_id = (gabe_id & 0x0F);
#endif 

    info->has_floppy = 1;
    info->has_expansion_card = 0;
    info->has_hard_drive = 1;
    info->has_ethernet = 1;
    info->screens = 2;


#if MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
    genx_model_id = *GABE_SUB_MODEL_FF_ID; // This gets the MID on the board that tells us if it is a PB/LB/CU
                                        // Now, that doesn't tell us if it is stuffed with audio chip or not
                                        // However, those will tell us about if there is a Floppy or not
                                        // This of course is good also for the A2560X
    genx_model_id = genx_model_id & GABE_SUB_FF_MASK;  // This isolate 2 bits, 
    // 00 = PB
    // 01 = LB
    // 10 = CU
    info->sub_model = genx_model_id;
#else 
    info->sub_model = 0;
#endif

    // Get the FPGA information
    info->fpga_date = /* YYYYMMDD */
        ((((*GABE_FIRMWARE_DATE) & GABE_FIRMWARE_DATE_YEAR_MASK) + 2000) << 16) |
        ((*GABE_FIRMWARE_DATE) & GABE_FIRMWARE_DATE_MONTH_MASK) |
        (((*GABE_FIRMWARE_DATE) & GABE_FIRMWARE_DATE_DAY_MASK) >> 16 );

    // Get the FPGA number and version/sub-version
    info->fpga_model = (*GABE_CHIP_VERSION & GABE_CHIP_N_MASK) >> 16;
    info->fpga_version = *GABE_CHIP_VERSION & GABE_CHIP_V_MASK;
    info->fpga_subver = (gabe_id & GABE_CHIP_SV_MASK) >> 16;

#if MODEL == MODEL_FOENIX_A2560K
    /* 2.1.1: the A2560K has 4 MB of SRAM, the memory the MCP and programs use (the 12 MB above was
     * wrong for it). Its 64 MB of SDRAM is not counted: the MCP does not use it yet. */
    info->system_ram_size = 4l * 1024l * 1024l;
#endif

#elif MODEL == MODEL_FOENIX_A2560U || MODEL == MODEL_FOENIX_A2560U_PLUS
    // All A2560Us made are actually A2560U+s
    // TODO: actually get this from GABE
    machine_id = MODEL_FOENIX_A2560U_PLUS;
    cpu = CPU_M68000;
    clock_speed = SYSCLK_20MHZ;

    info->has_floppy = 0;
    info->has_expansion_card = 0;
    info->has_hard_drive = 1;
    info->has_ethernet = 0;
    info->screens = 1;

    info->fpga_date = *FPGA_DATE_YEAR << 16 | *FPGA_DATE_MONTHDAY;
    info->fpga_model = *FPGA_MODEL_H << 16 | *FPGA_MODEL_L;
    info->fpga_version = *FPGA_VER;
    info->fpga_subver = *FPGA_SUBVER;

#elif MODEL == MODEL_FOENIX_FMX || MODEL == MODEL_FOENIX_C256U || MODEL == MODEL_FOENIX_C256U_PLUS
	machine_id = GABE_SYS_STAT->machine_id;
	cpu = CPU_WDC65816;
	clock_speed = SYSCLK_14MHZ;

	info->has_expansion_card = (GABE_SYS_STAT->no_expansion) ? 1 : 0;
    info->has_hard_drive = (*GABE_DIP_REG & HD_INSTALLED) ? 0 : 1;
    info->has_ethernet = 0;
    info->screens = 1;

	if (info->has_expansion_card) {
		// If an expansion card is present, find out what it is
		if (EXP_CARD_INFO->vendor_id == EXP_VENDOR_FOENIX) {
			switch (EXP_CARD_INFO->card_id) {
				case EXP_CARD_C100_ESID:
					// The ESID is plugged in, we add the SIDs and the ethernet port
					info->has_ethernet = 1;
					break;

				case EXP_CARD_C200_EVID:
					// The EVID is plugged in, we add the second screen and the ethernet port
					info->has_ethernet = 1;
					info->screens = 2;
					break;

				default:
					break;
			}
		}
	}

    info->fpga_model = GABE_VERSION->model;
    info->fpga_version = GABE_VERSION->version;
    info->fpga_subver = GABE_VERSION->subversion;

	switch (machine_id) {
		case MODEL_FOENIX_FMX:
			info->has_floppy = 1;
			info->system_ram_size = 12l * 1024l * 1024l;
			break;

		case MODEL_FOENIX_C256U_PLUS:
			info->has_floppy = 0;
			info->system_ram_size = 4l * 1024l * 1024l;
			break;

		default:
			info->has_floppy = 0;
			info->system_ram_size = 2l * 1024l * 1024l;
			break;
	}

#else
    machine_id = 0xFF;
    cpu = CPU_M68000;
    clock_speed = 0xFF;

    info->has_floppy = 0;
    info->has_expansion_card = 0;
    info->has_hard_drive = 0;
    info->has_ethernet = 0;
    info->screens = 1;

    info->fpga_model = 0;
    info->fpga_version = 0;
    info->fpga_subver = 0;
#endif

    info->model = machine_id;
    info->cpu = cpu;

    switch (clock_speed) {
        case SYSCLK_14MHZ:
            /* 14.318 MHz */
            info->cpu_clock_khz = 14318;
            break;

        case SYSCLK_20MHZ:
            info->cpu_clock_khz = 20000;
            break;

        case SYSCLK_25MHZ:
            info->cpu_clock_khz = 25000;
            break;

        case SYSCLK_33MHZ:
            info->cpu_clock_khz = 33000;
            break;

        case SYSCLK_40MHZ:
            info->cpu_clock_khz = 40000;
            break;

        case SYSCLK_50MHZ:
            info->cpu_clock_khz = 50000;
            break;

        case SYSCLK_66MHZ:
            info->cpu_clock_khz = 66000;
            break;

        case SYSCLK_80MHZ:
            info->cpu_clock_khz = 80000;
            break;

        default:
            info->cpu_clock_khz = 0;
            break;
    }
    info->bus_clock_khz = info->cpu_clock_khz;
#if MODEL == MODEL_FOENIX_A2560K
    /* 2.0.26 (owner 11-07): every K board reports MID 11; CPU_ID + CPU_SPEED_ID tell them apart:
       K25 = 68040V (6) at 25 MHz (2), K40 = 68040V (6) at 33 MHz (3), K60 = 68LC060 (11) at 66 MHz (6, bus 33.33 MHz) */
    if (cpu == CPU_M68LC060 && clock_speed == SYSCLK_66MHZ) {
        info->cpu_clock_khz = 66667;
        info->bus_clock_khz = 33333;
    }
#endif

    switch (info->cpu) {
        case CPU_WDC65816:
            info->cpu_name = "WDC65816";
            break;

        case CPU_M68000:
            info->cpu_name = "M68SEC000";
            break;

        case CPU_M68020:
            info->cpu_name = "M68020";
            break;

        case CPU_M68EC020:
            info->cpu_name = "M68EC020";
            break;

        case CPU_M68030:
            info->cpu_name = "M68030";
            break;

        case CPU_M680EC30:
            info->cpu_name = "M680EC30";
            break;

        case CPU_M68040:
            info->cpu_name = "M68040";
            break;

        case CPU_M68040V:
            info->cpu_name = "M68040V";
            break;

        case CPU_M680EC40:
            info->cpu_name = "M68EC040";
            break;

        case CPU_M68LC060:
            info->cpu_name = "MC68LC060";
            break;

        case CPU_486DX2_50:
            info->cpu_name = "i486DX2 50";
            break;

        case CPU_486DX2_60:
            info->cpu_name = "i486DX2 60";
            break;

        case CPU_486DX4:
            info->cpu_name = "i486DX4";
            break;

        /* CPU_MC68LC060 has the same value as CPU_M68LC060 (above): a second case for it is an
         * error (a duplicate case) for stricter compilers such as Calypsi
         */

        case CPU_MC68EC060:
            info->cpu_name = "MC68EC060";
            break;   
            
        case CPU_MC68060:
            info->cpu_name = "MC68060";
            break;              

        default:
            info->cpu_name = "UNKNOWN";
            break;
    }

    switch (info->model) {
        case MODEL_FOENIX_FMX:
            info->model_name = "C256 FMX";
            break;

        case MODEL_FOENIX_C256U:
            info->model_name = "C256 U";
            break;

        case MODEL_FOENIX_GENX:
            switch (genx_model_id) {
                case 0: info->model_name = "GenX32 - PB"; break;
                case 1: info->model_name = "GenX32 - LB"; break;
                case 2: info->model_name = "GenX32 - CUBE"; break;
                default: info->model_name = "GenX32"; break;
            }        
            break;

        case MODEL_FOENIX_C256U_PLUS:
            info->model_name = "C256 U+";
            break;

        case MODEL_FOENIX_A2560U_PLUS:
            info->model_name = "A2560 U+";
            break;

        case MODEL_FOENIX_A2560X:
            switch (genx_model_id) {
                case 0: info->model_name = "A2560X - PB"; break;
                case 1: info->model_name = "A2560X - LB"; break;
                case 2: info->model_name = "A2560X - CUBE"; break;
                default: info->model_name = "A2560 X"; break;
            }
            break;

        case MODEL_FOENIX_A2560U:
            info->model_name = "A2560 U";
            break;

        case MODEL_FOENIX_A2560K:
            if (info->cpu == CPU_M68040V && clock_speed == SYSCLK_25MHZ) info->model_name = "A2560K25";
            else if (info->cpu == CPU_M68040V && clock_speed == SYSCLK_33MHZ) info->model_name = "A2560K40";
            else if (info->cpu == CPU_M68LC060 && clock_speed == SYSCLK_66MHZ) info->model_name = "A2560K60";
            else if (info->cpu == CPU_M68LC060) info->model_name = "A2560K60 (FPGA before 1209)";
            else info->model_name = "A2560 K";
            break;

        default:
            info->model_name = "UNKNOWN";
            break;
    }
}

#if MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
static short genx_leds = 0;

/*
 * Set the color of the LED for the floppy drive
 *
 * Inputs:
 * colors = color specification, three bits: 0x_____RGB
 */
void genx_set_fdc_led(short colors) {
    genx_leds = (genx_leds & 0xFFF8) | (colors & 0x07);
    *GABE_GENX_STAT_LEDS = genx_leds;
}

/*
 * Set the color of the LED for the SD card slot
 *
 * Inputs:
 * colors = color specification, three bits: 0x_____RGB
 */
void genx_set_sdc_led(short colors) {
    genx_leds = (genx_leds & 0xFFC7) | ((colors & 0x07) << 3);
    *GABE_GENX_STAT_LEDS = genx_leds;
}

/*
 * Set the color of the LED for the IDE hard drive
 *
 * Inputs:
 * colors = color specification, three bits: 0x_____RGB
 */
void genx_set_hdc_led(short colors)  {
    genx_leds = (genx_leds & 0xFE3F) | ((colors & 0x07) << 6);
    *GABE_GENX_STAT_LEDS = genx_leds;
}
#endif

/**
 * Restart the machine, as the reset button does (2.1.0: REBOOT and Ctrl+Alt+Del).
 *
 * GAVIN's soft reset: a long write of $DEADxxxx with bit 15 (MANUAL_RESET) set to its control
 * register ($FEC0_0000) gives the same cold reset as the reset button (K25 hardware reference,
 * resets). The "DEAD" in the upper half guards against a stray write. The other bits are written
 * as they were (the power and SD LEDs), so nothing changes in the 10 ms before the reset.
 * Interrupts are switched off first, so nothing else runs in between.
 *
 * Other models: not supported; it just returns.
 */
void sys_reboot(void) {
#if MODEL == MODEL_FOENIX_A2560K
    unsigned long ctrl = *GABE_CTRL_REG & 0x7FFF;

    int_disable_all();
    *GABE_CTRL_REG = 0xDEAD0000 | MANUAL_RESET | ctrl;
    for (;;) {
        ctrl = *GABE_CTRL_REG;          /* wait: the reset comes within about 10 ms (the read keeps
                                           VBCC from calling this an empty, suspicious loop) */
    }
#endif
}
