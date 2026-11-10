# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: abronner <abronner@student.42berlin.de>    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/02/12 15:21:49 by abronner          #+#    #+#              #
#    Updated: 2026/02/12 16:20:46 by abronner         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

include colors.mk

ifeq ($(HOSTTYPE),)
	HOSTTYPE := $(shell uname -m)_$(shell uname -s)
endif

NAME		:= libft_malloc_$(HOSTTYPE).a

# * DIRECTORIES ************************************************************** #

SRC_DIR		:= src
OBJ_DIR		:= obj
INC_DIR		:= include

# * SOURCES & OBJECTS ******************************************************** #

vpath %.c $(SRC_DIR)
SRCS	:=

OBJS	:= $(SRCS:%.c=$(OBJ_DIR)/%.o)
DEPS	:= $(OBJS:.o=.d)

# * COMMANDS ***************************************************************** #

CC			:= cc
LD			:= cc
RM			:= rm -rf
AR			:= ar
PRNT		:= printf

# * FLAGS ******************************************************************** #

CFLAGS		?=	-Wall -Wextra -Werror

CPPFLAGS	:=
CPPFLAGS	+= -MMD -MP
CPPFLAGS	+= $(addprefix -I, $(INC_DIR))

LDFLAGS		:=

ifdef DEBUG
	CFLAGS += -O0
	CFLAGS += -g3
else
	CFLAGS += -O3
endif

ifeq ($(DEBUG), "UB")
	CFLAGS += -fsanitize=undefined
endif

ifeq ($(DEBUG), "ASAN")
	CFLAGS += -fsanitize=address
endif

ifeq ($(DEBUG), "FSAN")
	CFLAGS += -fsanitize=leak
endif

# * RULES ******************************************************************** #

all: $(NAME)

$(NAME): p_name

p_name: $(OBJS)
	@$(PRNT) "Creating library $(P_NAME).\n"

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@$(PRNT) "Creating $(P_ODIR) directory.\n"

$(OBJ_DIR)/%.o: %.c 
	@$(CC) $(CFLAGS) $(CPPFLAGS) $^ -c -o $@
	@$(PRNT) "Compiling $(P_AT).\n"

-include $(DEPS)

re: fclean all

clean:
	@$(RM) $(OBJ_DIR) $(DOTFILES)
	@$(PRNT) "Deleting object files for $(P_NAME).\n"

fclean: clean
	@$(RM) $(NAME)
	@$(PRNT) "Deleting library $(P_NAME).\n"

.PHONY: all bonus re clean fclean make \
	ctype std string split stack gnl sctach

# * PRINTING PRESETS ********************************************************************** #

P_NAME=$(C_RED)$(BLD)$(NAME)$(RESET)

P_ODIR=$(C_BLU)$(BLD)$(OBJ_DIR)$(RESET)

P_FLAG=$(C_MAG)$(BLD)-g$(RESET)

P_AT=$(C_GRN)$(BLD)$@$(RESET)
