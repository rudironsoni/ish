#!/usr/bin/env ruby
require "xcodeproj"

project = Xcodeproj::Project.open("iSH.xcodeproj")

# Find the libish_emu target
target = project.targets.find { |t| t.name == "libish_emu" }
unless target
  puts "ERROR: Target libish_emu not found"
  exit 1
end

puts "Found target: #{target.name}"

# Find the emu group
emu_group = project.main_group.find_subpath("emu", false) || project.main_group.new_group("emu")
aarch64_group = emu_group.find_subpath("aarch64", false) || emu_group.new_group("aarch64")

# Files to add
files_to_add = [
  "emu/aarch64/cpu.c",
  "emu/aarch64/memory.c",
  "emu/aarch64/tls.c",
  "emu/aarch64/block-cache.c"
]

added_count = 0

files_to_add.each do |filepath|
  filename = File.basename(filepath)

  # Check if file already exists in project
  existing = project.files.find { |f| f.path == filepath }
  if existing
    puts "✓ #{filename} already exists"
    next
  end

  # Create file reference
  file_ref = aarch64_group.new_file(filepath)
  puts "✓ Added #{filename}"

  # Add to target's sources build phase
  target.source_build_phase.add_file_reference(file_ref)
  puts "  Added to #{target.name} sources"
  added_count += 1
end

# Save the project
project.save
puts "\n✅ Saved #{added_count} new files to iSH.xcodeproj"
