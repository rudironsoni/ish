#!/usr/bin/env ruby
# Add asbestos/aarch64/*.c and *.h to iSH.xcodeproj
# This script safely modifies the Xcode project file

require 'xcodeproj'

PROJECT_PATH = 'iSH.xcodeproj'
TARGET_NAME = 'iSH'
AARCH64_DIR = 'asbestos/aarch64'

# Files to add
C_FILES = %w[
  gadgets_arith.c
  gadgets_entry.c
  gadgets_math.c
  gadgets_memory.c
  gen.c
]

H_FILES = %w[
  gadgets.h
  gadgets_tcti.h
  gen.h
]

# Large generated file (optional, can be generated at build time)
GENERATED_FILE = 'gadgets_tcti_impl.c'

puts "Opening #{PROJECT_PATH}..."
project = Xcodeproj::Project.open(PROJECT_PATH)

# Find the iSH target
target = project.targets.find { |t| t.name == TARGET_NAME }
unless target
  puts "ERROR: Target '#{TARGET_NAME}' not found"
  exit 1
end

puts "Found target: #{target.name}"

# Find the asbestos group (inside "most of the code")
main_code_group = project.main_group['most of the code']
unless main_code_group
  puts "ERROR: 'most of the code' group not found"
  exit 1
end

asbestos_parent = main_code_group['asbestos']
unless asbestos_parent
  puts "ERROR: 'asbestos' group not found"
  exit 1
end

asbestos_group = asbestos_parent['aarch64']
if asbestos_group
  puts "Group 'asbestos/aarch64' already exists"
else
  puts "Creating group 'asbestos/aarch64'..."
  asbestos_group = asbestos_parent.new_group('aarch64', 'asbestos/aarch64')
end

# Add header files to project
puts "Adding header files..."
H_FILES.each do |filename|
  filepath = "#{AARCH64_DIR}/#{filename}"

  # Check if file already exists in project
  existing = project.files.find { |f| f.path == filepath }
  if existing
    puts "  #{filename} already exists"
  else
    puts "  Adding #{filename}..."
    file_ref = asbestos_group.new_file(filepath)

    # Add to headers build phase if it exists
    if target.headers_build_phase
      target.headers_build_phase.add_file_reference(file_ref)
    end
  end
end

# Add C source files to project and target
puts "Adding source files..."
C_FILES.each do |filename|
  filepath = "#{AARCH64_DIR}/#{filename}"

  # Check if file already exists in project
  existing = project.files.find { |f| f.path == filepath }
  if existing
    puts "  #{filename} already exists"
  else
    puts "  Adding #{filename}..."
    file_ref = asbestos_group.new_file(filepath)

    # Add to sources build phase
    target.source_build_phase.add_file_reference(file_ref)
  end
end

# Handle generated file specially
# It may not exist yet (generated at build time)
generated_path = "#{AARCH64_DIR}/#{GENERATED_FILE}"
existing_generated = project.files.find { |f| f.path == generated_path }

if existing_generated
  puts "  #{GENERATED_FILE} already exists"
else
  puts "Adding generated file reference..."
  file_ref = asbestos_group.new_file(generated_path)
  target.source_build_phase.add_file_reference(file_ref)
end

# Add compiler flags for aarch64
puts "Updating build settings..."
target.build_configurations.each do |config|
  settings = config.build_settings

  # Add ARCH_AARCH64 define
  defines = settings['GCC_PREPROCESSOR_DEFINITIONS'] || ['$(inherited)']
  unless defines.include?('ARCH_AARCH64=1')
    defines << 'ARCH_AARCH64=1'
    settings['GCC_PREPROCESSOR_DEFINITIONS'] = defines
  end

  # Ensure the file is treated as C code
  settings['GCC_C_LANGUAGE_STANDARD'] = 'gnu11'
end

# Save the project
puts "Saving project..."
project.save

puts "Done!"
puts ""
puts "Added #{C_FILES.length} C source files and #{H_FILES.length} header files"
puts "to target '#{TARGET_NAME}'"
puts ""
puts "Next steps:"
puts "  1. Open iSH.xcodeproj in Xcode"
puts "  2. Clean build folder (Cmd+Shift+K)"
puts "  3. Build (Cmd+B)"
