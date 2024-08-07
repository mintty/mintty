        when 0x2500:  // ─ BOX DRAWINGS LIGHT HORIZONTAL
          boxlines(false, 0, 12, 24, 12, -1, -1);
        when 0x2501:  // ━ BOX DRAWINGS HEAVY HORIZONTAL
          boxlines(true, 0, 12, 24, 12, -1, -1);
        when 0x2502:  // │ BOX DRAWINGS LIGHT VERTICAL
          boxlines(false, 12, 0, 12, 24, -1, -1);
        when 0x2503:  // ┃ BOX DRAWINGS HEAVY VERTICAL
          boxlines(true, 12, 0, 12, 24, -1, -1);
        when 0x2504:  // ┄ BOX DRAWINGS LIGHT TRIPLE DASH HORIZONTAL
          boxlines(false, 0, 12, 3, 12, -1, -1);
          boxlines(false, 8, 12, 11, 12, -1, -1);
          boxlines(false, 16, 12, 19, 12, -1, -1);
          //boxlines(false, 0, 12, 4, 12, -1, -1);
          //boxlines(false, 8, 12, 12, 12, -1, -1);
          //boxlines(false, 16, 12, 20, 12, -1, -1);
        when 0x2505:  // ┅ BOX DRAWINGS HEAVY TRIPLE DASH HORIZONTAL
          boxlines(true, 0, 12, 3, 12, -1, -1);
          boxlines(true, 8, 12, 11, 12, -1, -1);
          boxlines(true, 16, 12, 19, 12, -1, -1);
          //boxlines(true, 0, 12, 4, 12, -1, -1);
          //boxlines(true, 8, 12, 12, 12, -1, -1);
          //boxlines(true, 16, 12, 20, 12, -1, -1);
        when 0x2506:  // ┆ BOX DRAWINGS LIGHT TRIPLE DASH VERTICAL
          boxlines(false, 12, 0, 12, 3, -1, -1);
          boxlines(false, 12, 8, 12, 11, -1, -1);
          boxlines(false, 12, 16, 12, 19, -1, -1);
          //boxlines(false, 12, 0, 12, 4, -1, -1);
          //boxlines(false, 12, 8, 12, 12, -1, -1);
          //boxlines(false, 12, 16, 12, 20, -1, -1);
        when 0x2507:  // ┇ BOX DRAWINGS HEAVY TRIPLE DASH VERTICAL
          boxlines(true, 12, 0, 12, 3, -1, -1);
          boxlines(true, 12, 8, 12, 11, -1, -1);
          boxlines(true, 12, 16, 12, 19, -1, -1);
          //boxlines(true, 12, 0, 12, 4, -1, -1);
          //boxlines(true, 12, 8, 12, 12, -1, -1);
          //boxlines(true, 12, 16, 12, 20, -1, -1);
        when 0x2508:  // ┈ BOX DRAWINGS LIGHT QUADRUPLE DASH HORIZONTAL
          boxlines(false, 0, 12, 2, 12, -1, -1);
          boxlines(false, 6, 12, 8, 12, -1, -1);
          boxlines(false, 12, 12, 14, 12, -1, -1);
          boxlines(false, 18, 12, 20, 12, -1, -1);
          //boxlines(false, 0, 12, 3, 12, -1, -1);
          //boxlines(false, 6, 12, 9, 12, -1, -1);
          //boxlines(false, 12, 12, 15, 12, -1, -1);
          //boxlines(false, 18, 12, 21, 12, -1, -1);
        when 0x2509:  // ┉ BOX DRAWINGS HEAVY QUADRUPLE DASH HORIZONTAL
          boxlines(true, 0, 12, 2, 12, -1, -1);
          boxlines(true, 6, 12, 8, 12, -1, -1);
          boxlines(true, 12, 12, 14, 12, -1, -1);
          boxlines(true, 18, 12, 20, 12, -1, -1);
          //boxlines(true, 0, 12, 3, 12, -1, -1);
          //boxlines(true, 6, 12, 9, 12, -1, -1);
          //boxlines(true, 12, 12, 15, 12, -1, -1);
          //boxlines(true, 18, 12, 21, 12, -1, -1);
        when 0x250A:  // ┊ BOX DRAWINGS LIGHT QUADRUPLE DASH VERTICAL
          boxlines(false, 12, 0, 12, 2, -1, -1);
          boxlines(false, 12, 6, 12, 8, -1, -1);
          boxlines(false, 12, 12, 12, 14, -1, -1);
          boxlines(false, 12, 18, 12, 20, -1, -1);
          //boxlines(false, 12, 0, 12, 3, -1, -1);
          //boxlines(false, 12, 6, 12, 9, -1, -1);
          //boxlines(false, 12, 12, 12, 15, -1, -1);
          //boxlines(false, 12, 18, 12, 21, -1, -1);
        when 0x250B:  // ┋ BOX DRAWINGS HEAVY QUADRUPLE DASH VERTICAL
          boxlines(true, 12, 0, 12, 2, -1, -1);
          boxlines(true, 12, 6, 12, 8, -1, -1);
          boxlines(true, 12, 12, 12, 14, -1, -1);
          boxlines(true, 12, 18, 12, 20, -1, -1);
          //boxlines(true, 12, 0, 12, 3, -1, -1);
          //boxlines(true, 12, 6, 12, 9, -1, -1);
          //boxlines(true, 12, 12, 12, 15, -1, -1);
          //boxlines(true, 12, 18, 12, 21, -1, -1);
        when 0x250C:  // ┌ BOX DRAWINGS LIGHT DOWN AND RIGHT
          boxlines(false, 12, 24, 12, 12, 24, 12);
        when 0x250D:  // ┍ BOX DRAWINGS DOWN LIGHT AND RIGHT HEAVY
        when 0x250E:  // ┎ BOX DRAWINGS DOWN HEAVY AND RIGHT LIGHT
        when 0x250F:  // ┏ BOX DRAWINGS HEAVY DOWN AND RIGHT
          boxlines(true, 12, 24, 12, 12, 24, 12);
        when 0x2510:  // ┐ BOX DRAWINGS LIGHT DOWN AND LEFT
          boxlines(false, 0, 12, 12, 12, 12, 24);
        when 0x2511:  // ┑ BOX DRAWINGS DOWN LIGHT AND LEFT HEAVY
        when 0x2512:  // ┒ BOX DRAWINGS DOWN HEAVY AND LEFT LIGHT
        when 0x2513:  // ┓ BOX DRAWINGS HEAVY DOWN AND LEFT
          boxlines(true, 0, 12, 12, 12, 12, 24);
        when 0x2514:  // └ BOX DRAWINGS LIGHT UP AND RIGHT
          boxlines(false, 12, 0, 12, 12, 24, 12);
        when 0x2515:  // ┕ BOX DRAWINGS UP LIGHT AND RIGHT HEAVY
        when 0x2516:  // ┖ BOX DRAWINGS UP HEAVY AND RIGHT LIGHT
        when 0x2517:  // ┗ BOX DRAWINGS HEAVY UP AND RIGHT
          boxlines(true, 12, 0, 12, 12, 24, 12);
        when 0x2518:  // ┘ BOX DRAWINGS LIGHT UP AND LEFT
          boxlines(false, 12, 0, 12, 12, 0, 12);
        when 0x2519:  // ┙ BOX DRAWINGS UP LIGHT AND LEFT HEAVY
        when 0x251A:  // ┚ BOX DRAWINGS UP HEAVY AND LEFT LIGHT
        when 0x251B:  // ┛ BOX DRAWINGS HEAVY UP AND LEFT
          boxlines(true, 12, 0, 12, 12, 0, 12);
        when 0x251C:  // ├ BOX DRAWINGS LIGHT VERTICAL AND RIGHT
          boxlines(false, 12, 0, 12, 24, -1, -1);
          boxlines(false, 12, 12, 24, 12, -1, -1);
        when 0x251D:  // ┝ BOX DRAWINGS VERTICAL LIGHT AND RIGHT HEAVY
        when 0x251E:  // ┞ BOX DRAWINGS UP HEAVY AND RIGHT DOWN LIGHT
        when 0x251F:  // ┟ BOX DRAWINGS DOWN HEAVY AND RIGHT UP LIGHT
        when 0x2520:  // ┠ BOX DRAWINGS VERTICAL HEAVY AND RIGHT LIGHT
        when 0x2521:  // ┡ BOX DRAWINGS DOWN LIGHT AND RIGHT UP HEAVY
        when 0x2522:  // ┢ BOX DRAWINGS UP LIGHT AND RIGHT DOWN HEAVY
        when 0x2523:  // ┣ BOX DRAWINGS HEAVY VERTICAL AND RIGHT
          boxlines(true, 12, 0, 12, 24, -1, -1);
          boxlines(true, 12, 12, 24, 12, -1, -1);
        when 0x2524:  // ┤ BOX DRAWINGS LIGHT VERTICAL AND LEFT
          boxlines(false, 12, 0, 12, 24, -1, -1);
          boxlines(false, 0, 12, 12, 12, -1, -1);
        when 0x2525:  // ┥ BOX DRAWINGS VERTICAL LIGHT AND LEFT HEAVY
        when 0x2526:  // ┦ BOX DRAWINGS UP HEAVY AND LEFT DOWN LIGHT
        when 0x2527:  // ┧ BOX DRAWINGS DOWN HEAVY AND LEFT UP LIGHT
        when 0x2528:  // ┨ BOX DRAWINGS VERTICAL HEAVY AND LEFT LIGHT
        when 0x2529:  // ┩ BOX DRAWINGS DOWN LIGHT AND LEFT UP HEAVY
        when 0x252A:  // ┪ BOX DRAWINGS UP LIGHT AND LEFT DOWN HEAVY
        when 0x252B:  // ┫ BOX DRAWINGS HEAVY VERTICAL AND LEFT
          boxlines(true, 12, 0, 12, 24, -1, -1);
          boxlines(true, 0, 12, 12, 12, -1, -1);
        when 0x252C:  // ┬ BOX DRAWINGS LIGHT DOWN AND HORIZONTAL
          boxlines(false, 0, 12, 24, 12, -1, -1);
          boxlines(false, 12, 12, 12, 24, -1, -1);
        when 0x252D:  // ┭ BOX DRAWINGS LEFT HEAVY AND RIGHT DOWN LIGHT
        when 0x252E:  // ┮ BOX DRAWINGS RIGHT HEAVY AND LEFT DOWN LIGHT
        when 0x252F:  // ┯ BOX DRAWINGS DOWN LIGHT AND HORIZONTAL HEAVY
        when 0x2530:  // ┰ BOX DRAWINGS DOWN HEAVY AND HORIZONTAL LIGHT
        when 0x2531:  // ┱ BOX DRAWINGS RIGHT LIGHT AND LEFT DOWN HEAVY
        when 0x2532:  // ┲ BOX DRAWINGS LEFT LIGHT AND RIGHT DOWN HEAVY
        when 0x2533:  // ┳ BOX DRAWINGS HEAVY DOWN AND HORIZONTAL
          boxlines(true, 0, 12, 24, 12, -1, -1);
          boxlines(true, 12, 12, 12, 24, -1, -1);
        when 0x2534:  // ┴ BOX DRAWINGS LIGHT UP AND HORIZONTAL
          boxlines(false, 0, 12, 24, 12, -1, -1);
          boxlines(false, 12, 0, 12, 12, -1, -1);
        when 0x2535:  // ┵ BOX DRAWINGS LEFT HEAVY AND RIGHT UP LIGHT
        when 0x2536:  // ┶ BOX DRAWINGS RIGHT HEAVY AND LEFT UP LIGHT
        when 0x2537:  // ┷ BOX DRAWINGS UP LIGHT AND HORIZONTAL HEAVY
        when 0x2538:  // ┸ BOX DRAWINGS UP HEAVY AND HORIZONTAL LIGHT
        when 0x2539:  // ┹ BOX DRAWINGS RIGHT LIGHT AND LEFT UP HEAVY
        when 0x253A:  // ┺ BOX DRAWINGS LEFT LIGHT AND RIGHT UP HEAVY
        when 0x253B:  // ┻ BOX DRAWINGS HEAVY UP AND HORIZONTAL
          boxlines(true, 0, 12, 24, 12, -1, -1);
          boxlines(true, 12, 0, 12, 12, -1, -1);
        when 0x253C:  // ┼ BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL
          boxlines(false, 0, 12, 24, 12, -1, -1);
          boxlines(false, 12, 0, 12, 24, -1, -1);
        when 0x253D:  // ┽ BOX DRAWINGS LEFT HEAVY AND RIGHT VERTICAL LIGHT
        when 0x253E:  // ┾ BOX DRAWINGS RIGHT HEAVY AND LEFT VERTICAL LIGHT
        when 0x253F:  // ┿ BOX DRAWINGS VERTICAL LIGHT AND HORIZONTAL HEAVY
        when 0x2540:  // ╀ BOX DRAWINGS UP HEAVY AND DOWN HORIZONTAL LIGHT
        when 0x2541:  // ╁ BOX DRAWINGS DOWN HEAVY AND UP HORIZONTAL LIGHT
        when 0x2542:  // ╂ BOX DRAWINGS VERTICAL HEAVY AND HORIZONTAL LIGHT
        when 0x2543:  // ╃ BOX DRAWINGS LEFT UP HEAVY AND RIGHT DOWN LIGHT
        when 0x2544:  // ╄ BOX DRAWINGS RIGHT UP HEAVY AND LEFT DOWN LIGHT
        when 0x2545:  // ╅ BOX DRAWINGS LEFT DOWN HEAVY AND RIGHT UP LIGHT
        when 0x2546:  // ╆ BOX DRAWINGS RIGHT DOWN HEAVY AND LEFT UP LIGHT
        when 0x2547:  // ╇ BOX DRAWINGS DOWN LIGHT AND UP HORIZONTAL HEAVY
        when 0x2548:  // ╈ BOX DRAWINGS UP LIGHT AND DOWN HORIZONTAL HEAVY
        when 0x2549:  // ╉ BOX DRAWINGS RIGHT LIGHT AND LEFT VERTICAL HEAVY
        when 0x254A:  // ╊ BOX DRAWINGS LEFT LIGHT AND RIGHT VERTICAL HEAVY
        when 0x254B:  // ╋ BOX DRAWINGS HEAVY VERTICAL AND HORIZONTAL
          boxlines(true, 0, 12, 24, 12, -1, -1);
          boxlines(true, 12, 0, 12, 24, -1, -1);
        when 0x254C:  // ╌ BOX DRAWINGS LIGHT DOUBLE DASH HORIZONTAL
          boxlines(false, 0, 12, 6, 12, -1, -1);
          boxlines(false, 12, 12, 18, 12, -1, -1);
        when 0x254D:  // ╍ BOX DRAWINGS HEAVY DOUBLE DASH HORIZONTAL
          boxlines(true, 0, 12, 6, 12, -1, -1);
          boxlines(true, 12, 12, 18, 12, -1, -1);
        when 0x254E:  // ╎ BOX DRAWINGS LIGHT DOUBLE DASH VERTICAL
          boxlines(false, 12, 0, 12, 6, -1, -1);
          boxlines(false, 12, 12, 12, 18, -1, -1);
        when 0x254F:  // ╏ BOX DRAWINGS HEAVY DOUBLE DASH VERTICAL
          boxlines(true, 12, 0, 12, 6, -1, -1);
          boxlines(true, 12, 12, 12, 18, -1, -1);
        when 0x2550:  // ═ BOX DRAWINGS DOUBLE HORIZONTAL
          boxlines(false, 0, dl, 24, dl, -1, -1);
          boxlines(false, 0, dh, 24, dh, -1, -1);
        when 0x2551:  // ║ BOX DRAWINGS DOUBLE VERTICAL
          boxlines(false, dl, 0, dl, 24, -1, -1);
          boxlines(false, dh, 0, dh, 24, -1, -1);
        when 0x2552:  // ╒ BOX DRAWINGS DOWN SINGLE AND RIGHT DOUBLE
        when 0x2553:  // ╓ BOX DRAWINGS DOWN DOUBLE AND RIGHT SINGLE
        when 0x2554:  // ╔ BOX DRAWINGS DOUBLE DOWN AND RIGHT
          boxlines(false, dl, 24, dl, dl, 24, dl);
          boxlines(false, dh, 24, dh, dh, 24, dh);
        when 0x2555:  // ╕ BOX DRAWINGS DOWN SINGLE AND LEFT DOUBLE
        when 0x2556:  // ╖ BOX DRAWINGS DOWN DOUBLE AND LEFT SINGLE
        when 0x2557:  // ╗ BOX DRAWINGS DOUBLE DOWN AND LEFT
          boxlines(false, 0, dl, dh, dl, dh, 24);
          boxlines(false, 0, dh, dl, dh, dl, 24);
        when 0x2558:  // ╘ BOX DRAWINGS UP SINGLE AND RIGHT DOUBLE
        when 0x2559:  // ╙ BOX DRAWINGS UP DOUBLE AND RIGHT SINGLE
        when 0x255A:  // ╚ BOX DRAWINGS DOUBLE UP AND RIGHT
          boxlines(false, dl, 0, dl, dh, 24, dh);
          boxlines(false, dh, 0, dh, dl, 24, dl);
        when 0x255B:  // ╛ BOX DRAWINGS UP SINGLE AND LEFT DOUBLE
        when 0x255C:  // ╜ BOX DRAWINGS UP DOUBLE AND LEFT SINGLE
        when 0x255D:  // ╝ BOX DRAWINGS DOUBLE UP AND LEFT
          boxlines(false, 0, dl, dl, dl, -1, -1);
          boxlines(false, dl, 0, dl, dl, -1, -1);
          boxlines(false, 0, dh, dh, dh, -1, -1);
          boxlines(false, dh, 0, dh, dh, -1, -1);
        when 0x255E:  // ╞ BOX DRAWINGS VERTICAL SINGLE AND RIGHT DOUBLE
        when 0x255F:  // ╟ BOX DRAWINGS VERTICAL DOUBLE AND RIGHT SINGLE
        when 0x2560:  // ╠ BOX DRAWINGS DOUBLE VERTICAL AND RIGHT
          boxlines(false, dl, 0, dl, 24, -1, -1);
          boxlines(false, dh, 0, dh, dl, 24, dl);
          boxlines(false, 24, dh, dh, dh, dh, 24);
        when 0x2561:  // ╡ BOX DRAWINGS VERTICAL SINGLE AND LEFT DOUBLE
        when 0x2562:  // ╢ BOX DRAWINGS VERTICAL DOUBLE AND LEFT SINGLE
        when 0x2563:  // ╣ BOX DRAWINGS DOUBLE VERTICAL AND LEFT
          boxlines(false, dl, 0, dl, dl, 0, dl);
          boxlines(false, 0, dh, dl, dh, dl, 24);
          boxlines(false, dh, 0, dh, 24, -1, -1);
        when 0x2564:  // ╤ BOX DRAWINGS DOWN SINGLE AND HORIZONTAL DOUBLE
        when 0x2565:  // ╥ BOX DRAWINGS DOWN DOUBLE AND HORIZONTAL SINGLE
        when 0x2566:  // ╦ BOX DRAWINGS DOUBLE DOWN AND HORIZONTAL
          boxlines(false, 0, dl, 24, dl, -1, -1);
          boxlines(false, 0, dh, dl, dh, dl, 24);
          boxlines(false, dh, 24, dh, dh, 24, dh);
        when 0x2567:  // ╧ BOX DRAWINGS UP SINGLE AND HORIZONTAL DOUBLE
        when 0x2568:  // ╨ BOX DRAWINGS UP DOUBLE AND HORIZONTAL SINGLE
        when 0x2569:  // ╩ BOX DRAWINGS DOUBLE UP AND HORIZONTAL
          boxlines(false, 0, dl, dl, dl, -1, -1);
          boxlines(false, dl, 0, dl, dl, -1, -1);
          boxlines(false, dh, 0, dh, dl, 24, dl);
          boxlines(false, 0, dh, 24, dh, -1, -1);
        when 0x256A:  // ╪ BOX DRAWINGS VERTICAL SINGLE AND HORIZONTAL DOUBLE
        when 0x256B:  // ╫ BOX DRAWINGS VERTICAL DOUBLE AND HORIZONTAL SINGLE
        when 0x256C:  // ╬ BOX DRAWINGS DOUBLE VERTICAL AND HORIZONTAL
          boxlines(false, 0, dl, dl, dl, -1, -1);
          boxlines(false, dl, 0, dl, dl, -1, -1);
          boxlines(false, dh, 0, dh, dl, 24, dl);
          boxlines(false, 0, dh, dl, dh, dl, 24);
          boxlines(false, dh, 24, dh, dh, 24, dh);
        when 0x256D:  // ╭ BOX DRAWINGS LIGHT ARC DOWN AND RIGHT
          boxcurve(2);
        when 0x256E:  // ╮ BOX DRAWINGS LIGHT ARC DOWN AND LEFT
          boxcurve(3);
        when 0x256F:  // ╯ BOX DRAWINGS LIGHT ARC UP AND LEFT
          boxcurve(4);
        when 0x2570:  // ╰ BOX DRAWINGS LIGHT ARC UP AND RIGHT
          boxcurve(1);
        when 0x2571:  // ╱ BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT
          boxlines(false, 0, 24, 24, 0, -2, -2);
        when 0x2572:  // ╲ BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT
          boxlines(false, 0, 0, 24, 24, -2, -2);
        when 0x2573:  // ╳ BOX DRAWINGS LIGHT DIAGONAL CROSS
          boxlines(false, 0, 24, 24, 0, -2, -2);
          boxlines(false, 0, 0, 24, 24, -2, -2);
        when 0x2574:  // ╴ BOX DRAWINGS LIGHT LEFT
          boxlines(false, 0, 12, 12, 12, -1, -1);
        when 0x2575:  // ╵ BOX DRAWINGS LIGHT UP
          boxlines(false, 12, 0, 12, 12, -1, -1);
        when 0x2576:  // ╶ BOX DRAWINGS LIGHT RIGHT
          boxlines(false, 12, 12, 24, 12, -1, -1);
        when 0x2577:  // ╷ BOX DRAWINGS LIGHT DOWN
          boxlines(false, 12, 12, 12, 24, -1, -1);
        when 0x2578:  // ╸ BOX DRAWINGS HEAVY LEFT
          boxlines(true, 0, 12, 12, 12, -1, -1);
        when 0x2579:  // ╹ BOX DRAWINGS HEAVY UP
          boxlines(true, 12, 0, 12, 12, -1, -1);
        when 0x257A:  // ╺ BOX DRAWINGS HEAVY RIGHT
          boxlines(true, 12, 12, 24, 12, -1, -1);
        when 0x257B:  // ╻ BOX DRAWINGS HEAVY DOWN
          boxlines(true, 12, 12, 12, 24, -1, -1);
        when 0x257C:  // ╼ BOX DRAWINGS LIGHT LEFT AND HEAVY RIGHT
        when 0x257D:  // ╽ BOX DRAWINGS LIGHT UP AND HEAVY DOWN
        when 0x257E:  // ╾ BOX DRAWINGS HEAVY LEFT AND LIGHT RIGHT
        when 0x257F:  // ╿ BOX DRAWINGS HEAVY UP AND LIGHT DOW

