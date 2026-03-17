.PHONY: gen open clean

# Regenerate the Xcode project from project.yml
gen:
	xcodegen generate

# Regenerate and open in Xcode
open: gen
	open DeviceAILabs.xcodeproj

# Nuke derived data for this project (fixes weird Xcode caching issues)
clean:
	rm -rf ~/Library/Developer/Xcode/DerivedData/DeviceAILabs-*
