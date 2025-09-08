mogrify -resize 32x32! -path resized icons/*.bmp
mogrify -resize 16x16! -path resized icons/*.bmp
convert +append resized/*.bmp icons/icons.png
