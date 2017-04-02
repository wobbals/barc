# CSS Layout management with barc

## Presets

There are several layouts available as preset parameters, loosely modeled after
the presets defined in https://tokbox.com/developer/beta/archive-custom-layout/

Notable additions include preset `auto` -- which will selectively use different
presets from the above. At each frame, a layout is chosen by the following
rubric: 

* If a stream is assigned the `focus` class, either explicitly or automatically
  from a classless screenshare (see Quirks, below), stylesheet preset `pip` is
  used, only if 0 or 1 non-`focus` streams accompany the focus stream.
* If a `focus` class stream is present along with more than one other stream,
  preset `horizontalPresentation` is used.
* If there are no `focus` streams on the frame, `bestfit` is used.

## CSS Attributes

Use `border-radius` to round the edges of a stream. Setting this value equal
to the height and width of the stream will produce a circle.

Use `object-fit` to define scaling behavior. The most common values are
`contain`, which will show all pixels from the source stream at the expense of
not always displaying the same dimensions as the element it is contained in,
and `cover`, which guarantees element size by cropping the source image to fit.
`fill` is also implemented, and will distort aspect ratio if the source video
does not match the size of the element.

## Quirks

There are a few behaviors to watch out for while using barc layout management.

### Height as percentage

When specifiying height as a percentage, the height of the output container is
used for the calculation, rather than the width. For example:

```css
stream.unfocus {
  width: 20%;
  height: 20%;
  object-fit: cover;
}
```

In proper CSS, stream elements matching the `unfocus` class would render as
squares, equal in height and width to 20% of the parent container's width (the
output rendering size). In barc, this spec will produce elements that match the
aspect ratio of the output container.

### Automatic focus assignment

If a stream is tagged as a screenshare in the manifest via `videoType: screen`,
and no layout class has been assigned to it, barc will automatically tag the
