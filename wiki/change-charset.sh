# This function can be defined in your shell profile, e.g. $HOME/.bashrc
# It can be invoked to change the character set of 
# mintty and the shell environment and keep them synchronous.

changecs () {
	# determine which locale environment variables are set
	lcvars=`env | sed -e "s,^LANG=.*,LANG," -e "s,^\(LC_[A-Z]*\)=.*,\1," -e t -e d`
	# change them to the new character set; if none is set, set LC_CTYPE
	for lcvar in ${lcvars:-LC_CTYPE}
	do	lc=`eval echo '$'$lcvar`
		lc=`echo $lc | sed -e "s,[.@].*,,"`.$1
		eval $lcvar=$lc
		eval export $lcvar
	done
	# get effective character set, checking locale environment variables 
	# in order of precedence
	lc="${LC_ALL:-${LC_CTYPE:-$LANG}}"
	# now set mintty character set
	echo -en "\033]701;$lc\007"
}

