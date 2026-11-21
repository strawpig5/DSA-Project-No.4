################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
msp430board_minheap.obj: C:/Users/abhis/Documents/GitHub/DSA-Project-No.4/MSP430\ Compliant\ Code/msp430board_minheap.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'MSP430 Compiler - building file: "$<"'
	"C:/ti/ccs2101/ccs/tools/compiler/ti-cgt-msp430_21.6.3.LTS/bin/cl430" -vmspx --data_model=restricted --use_hw_mpy=F5 --include_path="C:/ti/ccs2101/ccs/ccs_base/msp430/include" --include_path="C:/Users/abhis/workspace_ccstheia/DSA_Project_4" --include_path="C:/Users/abhis/workspace_ccstheia/DSA_Project_4/driverlib/MSP430F5xx_6xx" --include_path="C:/ti/ccs2101/ccs/tools/compiler/ti-cgt-msp430_21.6.3.LTS/include" --advice:power="none" --define=__MSP430F5529__ --define=DEPRECATED -g --printf_support=minimal --diag_warning=225 --diag_wrap=off --display_error_number --silicon_errata=CPU21 --silicon_errata=CPU22 --silicon_errata=CPU23 --silicon_errata=CPU40 --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


