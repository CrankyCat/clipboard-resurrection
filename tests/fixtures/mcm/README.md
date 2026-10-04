# English MCM regression inputs

These JSON files preserve the English presentation and non-display behavior
used before tokenizing the MCM configuration. `Build-Localization.py` resolves
the maintained `assets/MCM/Config/Clipboard` keys against the English catalog
and compares the resulting objects with these fixtures. They are never packaged.

Intentional UI changes must update the maintained configuration, English catalog,
translations and these comparison fixtures together. The fixtures must not be
regenerated automatically from the configuration they are meant to check.
