#!/bin/env bash
# Peter Hyman, pete@peterhyman.com
# Lucas Rademaker for MacOS and small systems compatibility.
# December 2020
# August 2021

# This program will return commit references based on Tags and Annotated Tags from git describe

usage() {
cat >&2  <<EOF
$(basename $0) command [-r]
all - entire git describe
commit - commit, omitting v
tagrev - tag revision count
major - major release version
ninor - minor release version
micro - micro release version
version - M.m.c
-r -- get release tag only
EOF
exit 1
}

# show message and usage
die() {
	echo "$1"
	usage
}

# return variables
# everything, with leading `v' and leading `g' for commits
describe_tag=
# abbreviated commit
commit=
# count of commits from last tag
tagrev=
# major version
major=
# minor version
minor=
# micro version
micro=
# get release or tag?
tagopt="--tags"

# get whole commit and parse
# if tagrev > 0 then add it and commit to micro version
# Expected format is:
# v#.#.#-#-g#######
init() {
	if [ -d '.git' ] ; then
		# Lucas Rademaker
		# If the below does not work using sed. comment the line
		# amd uncomment the three descrive_tag= lines below it.
		describe_tag=$(git describe $tagopt --long --abbrev=7 | sed -E 's/^v(.*?-)g(.*)$/\1\2/')
#		describe_tag=$(git describe $tagopt --long --abbrev=7)
#		describe_tag=${describe_tag/v/}
#		describe_tag=${describe_tag/g/}
		commit=$(echo $describe_tag | cut -d- -f3)
		tagrev=$(echo $describe_tag | cut -d- -f2)
		version=$(echo $describe_tag | cut -d- -f1)
		micro=$(echo $version | cut -d. -f3)
		[ $tagrev -gt 0 ] && micro=$micro-$tagrev-$commit
		minor=$(echo $version | cut -d. -f2)
		major=$(echo $version | cut -d. -f1)
	elif [ -r VERSION ] ; then
		major=$(awk '/Major: / {printf "%s",$2; exit}' VERSION)
		minor=$(awk '/Minor: / {printf "%s",$2; exit}' VERSION)
		micro=$(awk '/Micro: / {printf "%s",$2; exit}' VERSION)
	else
		echo "Cannot find .git or VERSION file. Aborting"
		exit 1
	fi
}

[ ! $(which git) ] && die "Something very wrong: git not found."

[ $# -eq 0 ] && die "Must provide a command and optional argument."

# are we getting a release only?
if [ $# -eq 2 ]; then
	if [ $2 = "-r" ]; then
		tagopt=""
	else
		die "Invalid option. Must be -r or nothing."
	fi
fi

init

case "$1" in
	"all" )
		retval=$describe_tag
		;;
	"commit" )
		retval=$commit
		;;
	"tagrev" )
		retval=$tagrev
		;;
	"version" )
		retval=$version
		;;
	"major" )
		retval=$major
		;;
	"minor" )
		retval=$minor
		;;
	"micro" )
		retval=$micro
		;;
	* )
		die "Invalid command."
		;;
esac

echo $retval

exit 0
