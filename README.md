# Batch Archive Processing

Individual stream archives require some post processing in order to be
consumable to end users. Barc is a tool for doing just that -- it can consume
an individual stream archive and produce a single composition container of the
media from the archive. Features include:

* Support for high-definition composed archives
* CSS-based layout management
  * Several predefined styles available for you to choose from
  * Custom stylesheets for complete customizability
* Simple web API and job queue for running a batch processing server
* 

# Archive layout presets

## Best fit
Automatically tries to fit as much content as possible to the viewport.

![bestFit](http://i.imgur.com/zeFBEMZ.png)

## Horizontal Presentation

Uses CSS class `focus` to give most of the viewport's real estate to a single
stream. If no class is provided, will automatically assign focus to any streams
with `videoType: screen` in the manifest.

![horizontalPresentation](http://i.imgur.com/bxuRdSh.png)

