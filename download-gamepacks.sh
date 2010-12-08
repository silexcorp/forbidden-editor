#!/bin/sh

# Usage:
#   sh download-gamepack.sh
#   LICENSEFILTER=GPL BATCH=1 sh download-gamepack.sh

: ${GIT:=git}
: ${SVN:=svn}
: ${WGET:=wget}
: ${ECHO:=echo}
: ${MKDIR:=mkdir}
: ${RM_R:=rm -f -r}
: ${MV:=mv}
: ${UNZIPPER:=unzip}

set -e

extra_urls()
{
	if [ -f "$1/extra-urls.txt" ]; then
		while IFS="	" read -r FILE URL; do
			$WGET -O "$1/$FILE" "$URL"
		done < "$1/extra-urls.txt"
	fi
}

pack()
{
	pack=$1; shift
	license=$1; shift
	sourcetype=$1; shift
	source=$1; shift

	if [ -d "games/$pack" ]; then
		$ECHO "Updating $pack..."
		case "$sourcetype" in
			svn)
				$SVN update "games/$pack" "$@" || true
				;;
			zip1)
				$RM_R zipdownload
				$MKDIR zipdownload
				cd zipdownload
				$WGET "$source" "$@" || true
				$UNZIPPER *.zip || true
				cd ..
				$RM_R "games/$pack"
				$MKDIR "games/$pack"
				$MV zipdownload/*/* "games/$pack/" || true
				$RM_R zipdownload
				;;
			gitdir)
				$RM_R "games/$pack"
				cd games
				$GIT archive --remote="$source" --prefix="$pack/" "$2":"$1" | tar xvf - || true
				cd ..
				;;
			git)
				cd "games/$pack"
				$GIT pull || true
				cd ../..
				;;
		esac
		extra_urls "games/$pack"
		return
	fi

	$ECHO
	$ECHO "Available pack: $pack"
	$ECHO "  License: $license"
	$ECHO "  Download via $sourcetype from $source"
	$ECHO
	case " $PACKFILTER " in
		"  ")
			;;
		*" $pack "*)
			;;
		*)
			$ECHO "Pack $pack rejected because it is not in PACKFILTER."
			return
			;;
	esac
	case " $LICENSEFILTER " in
		"  ")
			;;
		*" $license "*)
			;;
		*)
			$ECHO "Pack $pack rejected because its license is not in LICENSEFILTER."
			return
			;;
	esac
	case "$BATCH" in
		'')
			while :; do
				$ECHO "Download this pack? (y/n)"
				read -r P
				case "$P" in
					y*)
						break
						;;
					n*)
						return
						;;
				esac
			done
			;;
		*)
			;;
	esac
	
	$ECHO "Downloading $pack..."
	case "$sourcetype" in
		svn)
			$SVN checkout "$source" "games/$pack" "$@" || true
			;;
		zip1)
			$RM_R zipdownload
			$MKDIR zipdownload
			cd zipdownload
			$WGET "$source" "$@" || true
			$UNZIPPER *.zip || true
			cd ..
			$MKDIR "games/$pack"
			$MV zipdownload/*/* "games/$pack/" || true
			$RM_R zipdownload
			;;
		gitdir)
			cd games
			$GIT archive --remote="$source" --prefix="$pack/" "$2":"$1" | tar xvf - || true
			cd ..
			;;
		git)
			cd games
			$GIT clone "$source" "$pack" || true
			cd ..
			;;
	esac
	extra_urls "games/$pack"
	good=false
	for D in "games/$pack"/*.game; do
		if [ -d "$D" ]; then
			good=true
		fi
	done
	$good || rm -rf "$D"
}

mkdir -p games
pack DarkPlacesPack  GPL         svn    https://zerowing.idsoftware.com/svn/radiant.gamepacks/DarkPlacesPack/branches/1.5/
pack NexuizPack      GPL         gitdir git://git.icculus.org/divverent/nexuiz.git misc/netradiant-NexuizPack master
pack OpenArenaPack   unknown     zip1   http://ingar.satgnu.net/files/gtkradiant/gamepacks/OpenArenaPack.zip
pack Q3Pack          proprietary svn    https://zerowing.idsoftware.com/svn/radiant.gamepacks/Q3Pack/trunk/ -r29
pack Quake2Pack      proprietary zip1   http://ingar.satgnu.net/files/gtkradiant/gamepacks/Quake2Pack.zip
#pack Quake2WorldPack GPL         svn    svn://jdolan.dyndns.org/quake2world/trunk/gtkradiant
pack QuakePack       proprietary zip1   http://ingar.satgnu.net/files/gtkradiant/gamepacks/QuakePack.zip
pack TremulousPack   proprietary zip1   http://ingar.satgnu.net/files/gtkradiant/gamepacks/TremulousPack.zip
pack UFOAIPack       proprietary svn    https://zerowing.idsoftware.com/svn/radiant.gamepacks/UFOAIPack/branches/1.5/
#pack WarsowPack      GPL         svn    https://svn.bountysource.com/wswpack/trunk/netradiant/games/WarsowPack/
pack XonoticPack     GPL         git    git://git.xonotic.org/xonotic/netradiant-xonoticpack.git
