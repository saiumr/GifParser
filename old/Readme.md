## For Analyzing Gif Animation
### Compile On Windows (Use [Mingw64](https://winlibs.com/))    
```bash  
make creator  
./creator  

make parser  
./parser  
  
# you should ignore main and target, they are not this project theme, but it is useful for analyzing how to parse gif pic.   
# Additionally, they use SDL lib. Watch out sdl lib path, you should change it.  
make main  
./main  
  
make target  
./target  
```  
  
### `main.exe` operations  
**WatchOut: main and target use SDL2, but you should ignore them if you only care about GIF format**  
`q` for Quit  
`i` for Information of picture  
  
### Docs  
[Comprehend GIF format - This paper is enough](https://www.ihubin.com/blog/audio-video-basic-17-gif-file-format-detail/)  **Extremely Recommend!!**  
[GIF Overview1 - English reference "What is GIF"](http://giflib.sourceforge.net/whatsinagif/bits_and_bytes.html)  **Extremely Recommend!!**  
[GIF Overview2](https://blog.csdn.net/wzy198852/article/details/17266507)  
[How to storage imgine in GIF - LZW Compression](http://giflib.sourceforge.net/whatsinagif/lzw_image_data.html)  
[GIF Animation](http://giflib.sourceforge.net/whatsinagif/animation_and_transparency.html)  **Extremely Recommend!!**  
[GIF Example1](https://blog.csdn.net/GrayOnDream/article/details/123167897) **Recommend!!**  
[GIF Example2](https://www.jianshu.com/p/38743ef278ac)  
[BMP Overview](https://www.cnblogs.com/l2rf/p/5643352.html)  

### GIF Essentials  
Extension block start with `0x21`(ASCLL: `!`) and `Extension Label` follows it.  
`0xFF` is Application Extension.  
`0xF9` is Graphics Control Extension.   
`0xFE` is Comment Extension.  
`0x01` is Plain Text Extension.  
All extension block terminator is `0x00`.  

The `Image Descriptor` start with `0x2C`(ASCLL: `,`)  `Local Color Table` and `Image Data` follow it.   
`Image Data` terminator is also `0x00`, and there are data block (`block size` \+ `data byte`) processed by LZW algorithm maybe repeat many times.   

GIF file always terminated by a byte with a value of `0x3B`(ASCLL: `;`).  

Color index amount (Size of Color Table) must be power of 2, assume we use `M` colors and `size of color table` is `N`, which must $2^{(N+1)} \geq M$, if $N > M$ we set $N - M$ color(s) (RGB) occupy excess space but do not use that(those) color(s).

GIF uses index anf color table set each pixel color, color table max amount is $2^{7+1}=256$ because there are 3 bits set size of color table, and there are 3 bits set color resolution(or called it color depth), so GIF color has R(0~255) G(0~255) B(0~255), but it has colors max 256 because of size of color table.  

### test.gif  
it created by pragma `gif_creator`, and it will be parsed in pragma `gif_parser`  
![automatically generated images](test.gif)  

### Resource  
![dragon_cat_gif](lm.gif)   
