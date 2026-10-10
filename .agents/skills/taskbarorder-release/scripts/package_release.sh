#!/bin/sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 <zip-path> <version> [output-directory]" >&2
  exit 2
fi

zip_path=$1
version=$2
output_dir=${3:-release-output}

case "$version" in
  ''|*[!0-9.]*|.*|*.|*..*)
    echo "Version must use numeric x.y.z form." >&2
    exit 2
    ;;
esac

old_ifs=$IFS
IFS=.
set -- $version
IFS=$old_ifs
if [ "$#" -ne 3 ] || [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
  echo "Version must use numeric x.y.z form." >&2
  exit 2
fi

base="TaskbarOrder-$version-win11-x64"
expected_zip="$base.zip"
if [ ! -f "$zip_path" ] || [ ! -s "$zip_path" ]; then
  echo "Release ZIP is missing or empty: $zip_path" >&2
  exit 1
fi
if [ "$(basename "$zip_path")" != "$expected_zip" ]; then
  echo "Expected $expected_zip, got $(basename "$zip_path")" >&2
  exit 1
fi

if command -v 7zz >/dev/null 2>&1; then
  seven_zip=7zz
elif command -v 7z >/dev/null 2>&1; then
  seven_zip=7z
else
  echo "7-Zip is required (7zz or 7z)." >&2
  exit 1
fi

mkdir -p "$output_dir"
stage_dir="$output_dir/$base"
archive="$output_dir/$base.7z"
final="$output_dir/to.sa"
rm -rf "$stage_dir"
rm -f "$archive" "$final"
mkdir -p "$stage_dir"
cp "$zip_path" "$stage_dir/$expected_zip"

absolute_output=$(CDPATH= cd -- "$output_dir" && pwd)
(
  cd "$absolute_output"
  "$seven_zip" a -t7z -p'Abc*123' -mhe=on "$base.7z" "$base" >/dev/null
  "$seven_zip" t -p'Abc*123' "$base.7z" >/dev/null
  mv "$base.7z" to.sa
)

printf '%s\n' "$stage_dir/$expected_zip"
printf '%s\n' "$final"
