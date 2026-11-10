# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    colors.mk                                          :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: abronner <abronner@student.42berlin.de>    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/06/29 17:37:43 by abronner          #+#    #+#              #
#    Updated: 2025/06/29 17:38:34 by abronner         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

# * COLORS & FORMATTING ******************************************************************* #

#	UNIVERSAL RESET
RESET=\033[0m

#	FORMATTING: SECOND OPTION IS RESET

#	BOLD
BLD=\033[1m
R_BLD=\033[22m

#	DIM
DIM=\033[2m
R_DIM=\033[22m

#	ITALIC
ITA=\033[3m
R_ITA=\033[23m

#	UNDERSCORE
UND=\033[4m
R_UND=\033[24m

#	BLINKING
BLNK=\033[5m
R_BLNK=\033[25m

#	INVERSE
INV=\033[7m
R_INV=\033[27m

#	HIDDEN
HID=\033[8m
R_HID=\033[28m

#	STRIKETHROUGH
STRK=\033[9m
R_STRK=\033[29m

#	COLORS

#	BLACK
C_BLK=\033[30m
C_BLKBG=\033[40m

#	RED
C_RED=\033[31m
C_REDBG=\033[41m

#	GREEN
C_GRN=\033[32m
C_GRNBG=\033[42m

#	YELLOW
C_YLW=\033[33m
C_YLWBG=\033[43m

#	BLUE
C_BLU=\033[34m
C_BLUBG=\033[44m

#	MAGENTA
C_MAG=\033[35m
C_MAGBG=\033[45m

#	CYAN
C_CYA=\033[36m
C_CYABG=\033[46m

#	WHITE
C_WHI=\033[37m
C_WHIBG=\033[47m

#	DEFAULT
C_DEF=\033[39m
C_DEFBG=\033[49m
