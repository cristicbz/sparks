# This is free and unencumbered software released into the public domain.
# Based on the .ycm_extra_conf file provided for YouCompleteMe

import os
import glob
import ycm_core

# Path to the compilation database.
compilation_database_folder = 'build/debug'

database = ycm_core.CompilationDatabase( compilation_database_folder )

def DirectoryOfThisScript():
  return os.path.dirname( os.path.abspath( __file__ ) )

def MakeRelativePathsInFlagsAbsolute( flags, working_directory ):
  if not working_directory:
    return list( flags )
  new_flags = []
  make_next_absolute = False
  path_flags = [ '-isystem', '-I', '-iquote', '--sysroot=' ]
  for flag in flags:
    new_flag = flag

    if make_next_absolute:
      make_next_absolute = False
      if not flag.startswith( '/' ):
        new_flag = os.path.join( working_directory, flag )

    for path_flag in path_flags:
      if flag == path_flag:
        make_next_absolute = True
        break

      if flag.startswith( path_flag ):
        path = flag[ len( path_flag ): ]
        new_flag = path_flag + os.path.join( working_directory, path )
        break

    if new_flag:
      new_flags.append( new_flag )
  return new_flags

def QueryForFile( filename ):
  # Since .hpp files are not compiled we query the database for the
  # corresponding cpp file.
  is_header = len(filename) > 4 and filename[-4:] == '.hpp'
  if is_header:
    query_filename = filename[:-4] + '.cpp'
    if not os.path.exists(query_filename):
      cpp_files_in_path = glob.glob(os.path.join(os.path.dirname(filename),
                                                 '*.cpp'))
      if cpp_files_in_path:
        query_filename = cpp_files_in_path[0]
  else:
    query_filename = filename

  return query_filename


def FlagsForFile( filename ):
  query_filename = QueryForFile(filename)
  compilation_info = database.GetCompilationInfoForFile(query_filename)
  flags = MakeRelativePathsInFlagsAbsolute(
      compilation_info.compiler_flags_,
      compilation_info.compiler_working_dir_ )

  # Add system include paths and remove compiler name.
  flags = ['-isystem', '/usr/include/c++/4.8.1/',
           '-isystem', '/usr/include', '-std=c++11'] + flags[1:]

  # Replace mentions of query filename with filename.
  if query_filename != filename:
    flags = map(lambda s: s.replace(query_filename, filename), flags)

  return {
    'flags': flags,
    'do_cache': True
  }

