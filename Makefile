.PHONY: hooks ios

# Install git hooks for all developers on this repo.
# Run once after cloning: make hooks
hooks:
	@echo "Installing git hooks..."
	cp scripts/post-checkout .git/hooks/post-checkout
	cp scripts/post-merge    .git/hooks/post-merge
	chmod +x .git/hooks/post-checkout
	chmod +x .git/hooks/post-merge
	@echo "Done. Hooks active: post-checkout, post-merge"

# Shortcut to open the iOS sample in Xcode (regenerates first)
ios:
	cd samples/ios && make open
