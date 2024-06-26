#include "display-threads.h"

// NAMESPACE SETUP -----------------------------------------------------------
// ---------------------------------------------------------------------------
using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;
using rgb_matrix::FrameCanvas;
using ImageVector = std::vector<Magick::Image>;

// Passed to all threads that need to use the canvas
struct canvas_args canvas_ptrs;

extern volatile bool interrupt_received;

const char *img_filename = "/home/justin/rpi-rgb-led-matrix/examples-api-use/pixel_house.png";
const char *album_cover_filename = "/home/justin/rpi-rgb-led-matrix/examples-api-use/spotify_album.png";
const char *spotify_icon_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/spotify.png";
const char *spotify_pause_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/spotify_pause.png";
const char *spotify_play_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/spotify_play.png";
const char *spotify_default_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/spotify_default.png";

//const char *nyy_logo_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/nyy.png";
//const char *lad_logo_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/lad.png";
//const char *nym_logo_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/mets.png";
std::string mlb_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/";


ImageVector images;

// FIFO Setup
int cmd_fd;
int data_fd;

// FONTS
rgb_matrix::Font time_font;
rgb_matrix::Font date_font;
rgb_matrix::Font five_seven_font;
rgb_matrix::Font six_ten_font;
rgb_matrix::Font eight_thirteen_font;
rgb_matrix::Font *outline_font = NULL;

  // -------------------------------------------------------------------------
  // Given the filename, load the image and scale to the size of the matrix.
  // If this is an animated image, the resutlting vector will contain multiple.
  // -------------------------------------------------------------------------
static ImageVector LoadImageAndScaleImage(const char *filename,
                                          int target_width,
                                          int target_height) {
  ImageVector result;

  ImageVector frames;
  try {
    readImages(&frames, filename);
  } catch (std::exception &e) {
    if (e.what())
      fprintf(stderr, "%s\n", e.what());
    return result;
  }

  if (frames.empty()) {
    fprintf(stderr, "No image found.");
    return result;
  }

  // Animated images have partial frames that need to be put together
  if (frames.size() > 1) {
    Magick::coalesceImages(&result, frames.begin(), frames.end());
  } else {
    result.push_back(frames[0]); // just a single still image.
  }

  for (Magick::Image &image : result) {
    image.scale(Magick::Geometry(target_width, target_height));
  }

  return result;
}

  // ---------------------------------------------------------------------------
  // Copy an image to a Canvas. Note, the RGBMatrix is implementing the Canvas
  // interface as well as the FrameCanvas we use in the double-buffering of the
  // animted image.
  // ---------------------------------------------------------------------------
void CopyImageToCanvas(const Magick::Image &image, Canvas *canvas,
                       const int *x_pos, const int *y_pos) {

  // Copy all the pixels to the canvas.
  for (size_t y = 0; y < image.rows(); ++y) {
    for (size_t x = 0; x < image.columns(); ++x) {
      const Magick::Color &c = image.pixelColor(x, y);
      //printf("X: %d | Y: %d | ALPHAQUANTIM = %d\n", y, x, c.alphaQuantum());
//      if (c.alphaQuantum() < 70000) { // 256
        canvas->SetPixel(x + *x_pos, y + *y_pos,
                         ScaleQuantumToChar(c.redQuantum()),
                         ScaleQuantumToChar(c.greenQuantum()),
                         ScaleQuantumToChar(c.blueQuantum()));
//      }
    }
  }
  printf("Constructed image\n");
}

void CopyImageToCanvasTransparent(const Magick::Image &image, Canvas *canvas,
                       const int *x_pos, const int *y_pos) {

  // Copy all the pixels to the canvas.
  for (size_t y = 0; y < image.rows(); ++y) {
    for (size_t x = 0; x < image.columns(); ++x) {
      const Magick::Color &c = image.pixelColor(x, y);
      if (c.redQuantum()>0 && c.greenQuantum()>0 && c.blueQuantum()>0) { // 256
        canvas->SetPixel(x + *x_pos, y + *y_pos,
                         ScaleQuantumToChar(c.redQuantum()),
                         ScaleQuantumToChar(c.greenQuantum()),
                         ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }
  printf("Constructed image\n");
}


  // -------------------------------------------------------------------------
  // An animated image has to constantly swap to the next frame.
  // We're using double-buffering and fill an offscreen buffer first, then show.
  // -------------------------------------------------------------------------
void ShowAnimatedImage(const Magick::Image &image, RGBMatrix *canvas,
                       const int *x_pos, const int *y_pos,
                       FrameCanvas *offscreen_canvas) {
      CopyImageToCanvas(image, offscreen_canvas, x_pos, y_pos);
      offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
}

static bool FullSaturation(const rgb_matrix::Color &c) {
  return (c.r == 0 || c.r == 255)
    && (c.g == 0 || c.g == 255)
    && (c.b == 0 || c.b == 255);
}

void SetCanvasArea(FrameCanvas *offscreen_canvas, int x, int y,
                     int width, int height, rgb_matrix::Color *color) {
  for (int iy = 0; iy < height; ++iy) {
    for (int ix = 0; ix < width; ++ix) {
      offscreen_canvas->SetPixel(x + ix, y + iy, color->r, color->g, color->b);
    }
  }
}


void fontSetup() {
  // INITIALIZE FONTS
  const char *time_font_ptr = NULL;
  const char *date_font_ptr = NULL;
  const char *five_seven_ptr = NULL;
  const char *six_ten_ptr = NULL;
  const char *eight_thirteen_ptr = NULL;

  // Get font types
  char time_font_filepath[] = "/home/justin/rpi-rgb-led-matrix/fonts/7x14B.bdf";
  time_font_ptr = strdup(time_font_filepath);
  if (time_font_ptr == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return;
  }
  char date_font_filepath[] = "/home/justin/rpi-rgb-led-matrix/fonts/4x6.bdf";
  date_font_ptr = strdup(date_font_filepath);
  if (date_font_ptr == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return;
  }

  char five_seven_filepath[] = "/home/justin/rpi-rgb-led-matrix/fonts/5x7.bdf";
  five_seven_ptr = strdup(five_seven_filepath);
  if (date_font_ptr == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return;
  }

  char six_ten_filepath[] = "/home/justin/rpi-rgb-led-matrix/fonts/6x10.bdf";
  six_ten_ptr = strdup(six_ten_filepath);
  if (six_ten_ptr == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return;
  }

  char eight_thirteen_filepath[] = "/home/justin/rpi-rgb-led-matrix/fonts/9x15B.bdf";
  eight_thirteen_ptr = strdup(eight_thirteen_filepath);
  if (eight_thirteen_ptr == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return;
  }

  // Load font. This needs to be a filename with a bdf bitmap font.
  if (!time_font.LoadFont(time_font_ptr)) {
    fprintf(stderr, "Couldn't load font '%s'\n", time_font_ptr);
    return;
  }

  if (!date_font.LoadFont(date_font_ptr)) {
    fprintf(stderr, "Couldn't load font '%s'\n", date_font_ptr);
    return;
  }

  if (!five_seven_font.LoadFont(five_seven_ptr)) {
    fprintf(stderr, "Couldn't load font '%s'\n", five_seven_ptr);
    return;
  }

  if (!six_ten_font.LoadFont(six_ten_ptr)) {
    fprintf(stderr, "Couldn't load font '%s'\n", six_ten_ptr);
    return;
  }

  if (!eight_thirteen_font.LoadFont(eight_thirteen_ptr)) {
    fprintf(stderr, "Couldn't load font '%s'\n", eight_thirteen_ptr);
    return;
  }

}

// FUNCTION SETUP - THREADS --------------------------------------------------
// ---------------------------------------------------------------------------
void* clockThread(void *ptr){

  printf("entered clock thread");

  struct canvas_args *canvas_ptrs = (struct canvas_args*)ptr;
  RGBMatrix *canvas = canvas_ptrs->canvas;
  FrameCanvas *offscreen_canvas = canvas_ptrs->offscreen_canvas;
  pthread_mutex_t canvas_mutex = canvas_ptrs->canvas_mutex;

  // INITIALIZE CLOCK VARIABLES
  static const char wday_name[][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };
  static const char mon_name[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };

  // Related to time.h struct
  struct tm* time_ptr;
  time_t t;

  // Configure colors for text
  rgb_matrix::Color color(180, 90, 0); // text color
  rgb_matrix::Color bg_color(0, 0, 0);
  rgb_matrix::Color flood_color(0, 0, 0);
  rgb_matrix::Color outline_color(0,0,0);
  bool with_outline = false;

  // Positions for the time on the canvas
  int time_x = 0;
  int time_y = 52;

  int date_x = 0;
  int date_y = 47;

  int sec_x = 42;
  int sec_y = 54;

  char time_str[1024];
  char sec_str[1024];
  char date_str[1024];

  int letter_spacing = 0;

  // PUT TIME ONTO CANVAS
  // PUT TIME ONTO CANVAS
  while(!interrupt_received) {
    // Get current time
    t = time(NULL);
    time_ptr = localtime(&t);

    // Update char arrays w/ new data
    sprintf(time_str, "%.2d:%.2d:", time_ptr->tm_hour, time_ptr->tm_min);
    sprintf(sec_str, "%.2d AM", time_ptr->tm_sec);
    sprintf(date_str, "%.3s %.3s %.2d %d", wday_name[time_ptr->tm_wday], mon_name[time_ptr->tm_mon],
                                           time_ptr->tm_mday, 1900+(time_ptr->tm_year));

    pthread_mutex_lock(&canvas_mutex);
    printf("Clock at MUTEX\n");
   // Update the time to the display
    rgb_matrix::DrawText(offscreen_canvas, time_font, time_x, time_y + time_font.baseline(),
                         color, outline_font ? NULL : &bg_color, time_str,
                         letter_spacing);

    rgb_matrix::DrawText(offscreen_canvas, date_font, date_x, date_y + date_font.baseline(),
                         color, outline_font ? NULL : &bg_color, date_str,
                         letter_spacing);

    rgb_matrix::DrawText(offscreen_canvas, date_font, sec_x, sec_y + date_font.baseline(),
                         color, outline_font ? NULL : &bg_color, sec_str,
                         letter_spacing);

    //offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
    canvas->SwapOnVSync(offscreen_canvas);

    pthread_mutex_unlock(&canvas_mutex);
    usleep(200*1000);
  }
  printf("Exited clock thread");
}

  // -------------------------------------------------------------------------
  // Thread for the image generation on the display, displays the image on the
  // display portion.
  // -------------------------------------------------------------------------
void* imageThread(void *ptr){

  printf("Entered image thread");

  struct canvas_args *canvas_ptrs = (struct canvas_args*)ptr;
  RGBMatrix *canvas = canvas_ptrs->canvas;
  FrameCanvas *offscreen_canvas = canvas_ptrs->offscreen_canvas;
  pthread_mutex_t canvas_mutex = canvas_ptrs->canvas_mutex;

  Magick::InitializeMagick(NULL);

  const int offset_x = 0;
  const int offset_y = 0;

  int image_display_count = 0;
  int still_image_sleep = 1000000;

  while(!interrupt_received) {

    // PUT IMAGE ONTO CANVAS
    pthread_mutex_lock(&canvas_mutex);
    printf("Image at MUTEX\n");

    switch (images.size()) {
    case 0:   // failed to load image.
      pthread_mutex_unlock(&canvas_mutex);
      break;
    case 1:   // Simple example: one image to show
      // Shanty code with the delays, but it works

      CopyImageToCanvas(images[0], offscreen_canvas, &offset_x, &offset_y);
      canvas->SwapOnVSync(offscreen_canvas);
      pthread_mutex_unlock(&canvas_mutex);

      usleep(still_image_sleep*3);
      break;
    default:  // More than one image: this is an animation.
     //FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();
     for (const auto &image : images) {
       ShowAnimatedImage(image,canvas, &offset_x, &offset_y, offscreen_canvas);
       pthread_mutex_unlock(&canvas_mutex);
       usleep(image.animationDelay() * 10000);  // 1/100s converted to usec
     }
      break;
    }
  }
  printf("Exited image thread");
}

  // -------------------------------------------------------------------------
  // Thread for displaying data from Spotify music. Uses data from the FIFO
  // with the Python code to grab data from the API.
  // -------------------------------------------------------------------------
void* spotifyThread(void *ptr){
  printf("Entered spotify thread");

  struct canvas_args *canvas_ptrs = (struct canvas_args*)ptr;
  RGBMatrix *canvas = canvas_ptrs->canvas;
  FrameCanvas *offscreen_canvas = canvas_ptrs->offscreen_canvas;
  pthread_mutex_t canvas_mutex = canvas_ptrs->canvas_mutex;

  Magick::InitializeMagick(NULL);

  const int album_cover_x = 0;
  const int album_cover_y = 0;
  const int album_cover_size = 32;
  const int spotify_pauseplay_x = 44;
  const int spotify_pauseplay_y = 6;
  const int spotify_logo_x = 55;
  const int spotify_logo_y = 0;
  const int progress_bar_x = 36;
  const int progress_bar_y = 22;
  const int song_x_orig = 0;
  const int song_y_orig = 35;
  const int artist_x_orig = 34;
  const int artist_y_orig = 26;

  int song_x = song_x_orig;
  int song_y = song_y_orig;
  int artist_x = artist_x_orig;
  int arist_y = artist_y_orig;

  int song_len;
  int artist_len;

  int letter_spacing = 0;
  const int refresh_sleep = 1000000;

  float song_count = 0;
  float artist_count = 0;
  float usleep_count = 0;
  const int cmd_refresh = 3;

  rgb_matrix::Color progress_color(100, 100, 100);
  rgb_matrix::Color progress_left_color(211, 91, 68);
  rgb_matrix::Color progress_bar_color(200,200,200);
  rgb_matrix::Color bg_color(0, 0, 0);

  ImageVector spotify_default = LoadImageAndScaleImage(spotify_default_fn,
                                                       album_cover_size,
                                                       album_cover_size);

  ImageVector album_cover = LoadImageAndScaleImage(album_cover_filename,
                                                   album_cover_size,
                                                   album_cover_size);

  ImageVector spotify_logo = LoadImageAndScaleImage(spotify_icon_fn,
                                                    9, 9);
  ImageVector spotify_pause = LoadImageAndScaleImage(spotify_pause_fn,
                                                     9, 14);
  ImageVector spotify_play = LoadImageAndScaleImage(spotify_play_fn,
                                                     9, 14);

  std::string prev_song_name = "meep";
  std::string song_name;
  std::string artist_name;
  bool music_playing = true;
  int track_length = 0; // in seconds
  int track_progress = 0;
  bool track_progress_bar[24] = {false};


  char cmd_tx[64] = "spotify_curr_playing";
  char data_rx[1024];

  int status_code = 0;

  // BEFORE DRAWING TEXT
  while(!interrupt_received){

    // TWO COUNTS RUNNING: one for scrolling text (200ms)
    // one for sending API requests (3s)

    pthread_mutex_lock(&canvas_mutex);

    printf("SPOTIFY AT MUTEX\n");

    if((int)usleep_count > cmd_refresh) {

      // OPEN THE FIFOS
      cmd_fd = open("/home/justin/rpi-rgb-led-matrix/examples-api-use/cmd_cc_to_py",O_WRONLY|O_NONBLOCK);
      printf("Cmd fifo opened on C++\n");
      data_fd = open("/home/justin/rpi-rgb-led-matrix/examples-api-use/data_py_to_cc",O_RDONLY|O_NONBLOCK);
      printf("Data fifo opened on C++\n");

      printf("C++ sending command for song data\n");
      write(cmd_fd, cmd_tx, sizeof(cmd_tx));

      printf("Reading from data fifo\n");

      usleep(500*1000); // 50ms
      read(data_fd, data_rx, 1024);
      printf("Received %s", data_rx);
      // PARSE FIFO DATA
      char *token;
      // Split string into array of strings
      token = strtok(data_rx,",");
     // std::string data(token);

      if(token != NULL){
        std::string data(token);
        status_code = stoi(data);
        token = strtok(NULL,",");
      } else {
        status_code = 0;
      }
      //token = strtok(NULL,",");

      // 200 = currently playing song returned
      // 204 = no song currently playing
      switch(status_code) {
        case 200:
          for(int i = 1; i<6; i++){
            printf("i = %d\n", i);
            if(token != NULL) {
              std::string data(token);
              switch(i) {
                case 1:
                  track_progress = stoi(data);
                  track_progress /= 1000;  // ms to s
                  break;
                case 2:
                  track_length = stoi(data);
                  track_length /= 1000;
                  break;
                case 3:
                  prev_song_name = song_name;
                  song_name = data;
                  break;
                case 4:
                  artist_name = data;
                  break;
                case 5:
                  printf("Data = %s\n", data.c_str());
                  if(data == "False") {
                    music_playing = false;
                  } else {
                    music_playing = true;
                  }
                  printf("Music playing: %d\n", music_playing);
                  break;
                }
                token = strtok(NULL,",");
              } else i=6;
            }
          break;
        case 204:
          track_progress = 0;
          track_length = 100;
          music_playing = false;
          artist_name = " ";
          song_name = " ";
          break;
        }
      // Check if new song, if so then update the album cover
      if(song_name != prev_song_name) {
        song_x = song_x_orig;
        artist_x = artist_x_orig;

        album_cover = LoadImageAndScaleImage(album_cover_filename,
                                             album_cover_size,
                                             album_cover_size);
        printf("IMAGE UPDATED\n");

      }

      // RESUME ITEMS SENSITIVE TO 3S COUNT
      // TWO timers running:
      // one for scrolling the texts
      // one for sending a new REST request

      if((int)usleep_count > cmd_refresh) {

      // Change pause or play depending on the state of music playing or not
      if(music_playing) {
        CopyImageToCanvas(spotify_pause[0], offscreen_canvas, &spotify_pauseplay_x, &spotify_pauseplay_y);
      } else {
        CopyImageToCanvas(spotify_play[0], offscreen_canvas, &spotify_pauseplay_x, &spotify_pauseplay_y);
      }

      // Check track progress and update if needed
      if(track_progress > track_length) {
        track_progress = 0;
        for(int i = 0; i<sizeof(track_progress_bar); i++) {
          track_progress_bar[i] = false;
        }
      } else {
        float track_segments = track_length/24;
        for(int i = 0; i<sizeof(track_progress_bar); i++) {
          if((float)track_progress > track_segments*i) {
            track_progress_bar[i] = true;
          } else {
            track_progress_bar[i] = false;
          }
        }
      }

      // Update display based upon track progress
      for(int i = 0; i<sizeof(track_progress_bar); i++) {
        if(track_progress_bar[i]) {
          // sets grey "played"
          canvas->SetPixel(progress_bar_x+i, progress_bar_y, progress_color.r,
                           progress_color.g, progress_color.b);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y+1, progress_color.r,
                           progress_color.g, progress_color.b);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y-1, 0, 0, 0);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y+2, 0, 0, 0);

        } else if ((i!=0 && track_progress_bar[i-1]) || (i==0 && !track_progress_bar[0]) ) {
          // Sets pressent bar
          for(int x = 0; x<=1; x++) {
            for(int y = -1; y<=2; y++) {
              canvas->SetPixel((progress_bar_x+i+x), (progress_bar_y+y), progress_bar_color.r,
                                progress_bar_color.g, progress_bar_color.b);
            }
          }
          canvas->SetPixel(progress_bar_x+i-1, progress_bar_y-1, 0, 0, 0);
          canvas->SetPixel(progress_bar_x+i-1, progress_bar_y+2, 0, 0, 0);
          i++; // if not, pink overwrites the player bar

        } else {
          // sets pink "unplayed"
          canvas->SetPixel(progress_bar_x+i, progress_bar_y, progress_left_color.r,
                           progress_left_color.g, progress_left_color.b);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y+1, progress_left_color.r,
                           progress_left_color.g, progress_left_color.b);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y-1, 0, 0, 0);
          canvas->SetPixel(progress_bar_x+i, progress_bar_y+2, 0, 0, 0);

        }
      }

      // Clear the very last bar - incase the track progress bar hits the end
      if(!track_progress_bar[22]) {
        for(int i = -1; i<=2; i++) {
          canvas->SetPixel(progress_bar_x+24, progress_bar_y+i, 0, 0, 0);
        }
      }

      track_progress += cmd_refresh;
      usleep_count = 0;


    }
    }

    // Clear text before adding new text
    SetCanvasArea(offscreen_canvas, song_x_orig, song_y_orig, 64, 7, &bg_color);

    // Artist and song name
    song_len = rgb_matrix::DrawText(offscreen_canvas, five_seven_font, song_x, song_y_orig + five_seven_font.baseline(),
                         progress_bar_color, outline_font ? NULL : &bg_color, song_name.c_str(), letter_spacing);

    SetCanvasArea(offscreen_canvas, 0, artist_y_orig, 64/*artist_x_orig-1*/, 6, &bg_color);

    artist_len = rgb_matrix::DrawText(offscreen_canvas, date_font, artist_x, artist_y_orig + date_font.baseline(),
                         progress_bar_color, outline_font ? NULL : &bg_color, artist_name.c_str(),
                         letter_spacing);

    SetCanvasArea(offscreen_canvas, artist_x_orig-2, artist_y_orig, 2, 6, &bg_color);

    if(status_code == 200) {
      // Album cover and spotify logo
      CopyImageToCanvas(album_cover[0], offscreen_canvas, &album_cover_x, &album_cover_y);
    } else {
      CopyImageToCanvas(spotify_default[0], offscreen_canvas, &album_cover_x, &album_cover_y);
    }

    CopyImageToCanvas(spotify_logo[0], offscreen_canvas, &spotify_logo_x, &spotify_logo_y);

    // Scrolls text across screen

    if(((int)song_count>10) && (song_len>64) && ((--song_x+song_len) < 0)) {
      song_x = song_x_orig;
      song_count = 0;
    }

    if(((int)artist_count>10) && (artist_len>64-artist_x_orig) && ((--artist_x+artist_len-artist_x_orig) < 0)) {
      artist_x = artist_x_orig;
      artist_count = 0;
    }


    canvas->SwapOnVSync(offscreen_canvas);
    pthread_mutex_unlock(&canvas_mutex);

    usleep(200*1000);
    usleep_count += 0.2;
    song_count += 0.2;
    artist_count += 0.2;
    //usleep(refresh_sleep*3);
    //track_progress += 3;

  }

  close(data_fd);
  close(cmd_fd);
  printf("Exited spotify thread");
}


void* baseballThread(void* ptr) {
  printf("Entered baseball thread");

  // BASES, INNINGS, AND BALL/STRIKE OUTPUTS
  const char *empty_base_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/empty_base.png";
  const char *full_base_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/full_base.png";
  const char *bot_inning_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/bot_inning.png";
  const char *top_inning_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/top_inning.png";

  const char *empty_circle_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/empty_circle.png";
  const char *ball_circle_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/ball_circle.png";
  const char *strike_circle_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/strike_circle.png";
  const char *out_circle_fn = "/home/justin/rpi-rgb-led-matrix/examples-api-use/out_circle.png";


  struct canvas_args *canvas_ptrs = (struct canvas_args*)ptr;
  RGBMatrix *canvas = canvas_ptrs->canvas;
  FrameCanvas *offscreen_canvas = canvas_ptrs->offscreen_canvas;
  pthread_mutex_t canvas_mutex = canvas_ptrs->canvas_mutex;

  Magick::InitializeMagick(NULL);

  int letter_spacing = 0;

  // GAME INFO
  const int base_size = 9;
  const int first_base_x = 36;
  const int first_base_y = 7;
  const int second_base_x = first_base_x - 5;
  const int second_base_y = first_base_y - 5;
  const int third_base_x = first_base_x - 10;
  const int third_base_y = first_base_y;

  const int inning_h = 3;
  const int inning_w = 5;
  const int inning_x = 19;
  const int top_inning_y = 2;
  const int bot_inning_y = top_inning_y + 12;

  // BALL STRIKE OUT
  const int circles_text_x = 20;
  const int first_cir_x = circles_text_x+5;
  const int second_cir_x = first_cir_x+5;
  const int third_cir_x = second_cir_x+5;
  const int ball_y = 19;
  const int strike_y = ball_y + 6;
  const int out_y = strike_y + 6;
  const int circle_size = 4;

  // SCOREBOARDS
  const int away_x = 1;
  const int away_y = 1;
  const int home_x = 47;
  const int home_y = 1;
  const int logo_size = 16;
  const int center_x = 19;
  const int center_y = 0;

  // IMAGES

  std::string home_logo_fn;
  std::string away_logo_fn;
  bool new_teams = true;

  ImageVector home_logo;
  ImageVector away_logo;

/*
  ImageVector home_logo = LoadImageAndScaleImage(nyy_logo_fn,
                                    logo_size,
                                    logo_size);

  ImageVector away_logo = LoadImageAndScaleImage(lad_logo_fn,
                                    logo_size,
                                    logo_size);
*/

  ImageVector empty_base = LoadImageAndScaleImage(empty_base_fn,
                                    base_size,
                                    base_size);

  ImageVector full_base = LoadImageAndScaleImage(full_base_fn,
                                    base_size,
                                    base_size);

  ImageVector top_inning = LoadImageAndScaleImage(top_inning_fn,
                                    inning_w,
                                    inning_h);

  ImageVector bot_inning = LoadImageAndScaleImage(bot_inning_fn,
                                    inning_w,
                                    inning_h);

  ImageVector empty_circle = LoadImageAndScaleImage(empty_circle_fn,
                                    circle_size,
                                    circle_size);

  ImageVector ball_circle = LoadImageAndScaleImage(ball_circle_fn,
                                    circle_size,
                                    circle_size);

  ImageVector strike_circle = LoadImageAndScaleImage(strike_circle_fn,
                                    circle_size,
                                    circle_size);

  ImageVector out_circle = LoadImageAndScaleImage(out_circle_fn,
                                    circle_size,
                                    circle_size);

  // COLORS
  rgb_matrix::Color home_main(12, 35, 64);
  rgb_matrix::Color away_main(0, 90, 156);

  rgb_matrix::Color home_second(200,200,200);
  rgb_matrix::Color away_second(225, 225, 225);

  rgb_matrix::Color scoreboard_color(143, 143, 143);
  rgb_matrix::Color blank(0,0,0);


  // GAME DATA
  std::string home_abrv = "NYY";
  std::string away_abrv = "LAD";

  std::string home_score;// = "10";
  std::string away_score;// = "4";
  std::string inning;// = "8";

  std::string first_pitch_time;
  std::string am_pm;

  std::string game_state;

  char b[] = "B";
  char s[] = "S";
  char o[] = "O";

  int balls = 0;
  int strikes = 0;
  int outs = 0;

  bool on_first;
  bool on_second;
  bool on_third;

  int away_runs;
  int home_runs;

  int innings;

  // FIFO DATA
  char cmd_tx[64] = "mlb";
  char data_rx[1024];

  while(!interrupt_received) {
    pthread_mutex_lock(&canvas_mutex);
    printf("BASEBALL AT MUTEX\n");

    cmd_fd = open("/home/justin/rpi-rgb-led-matrix/examples-api-use/cmd_cc_to_py",O_WRONLY|O_NONBLOCK);
    printf("Cmd fifo opened on C++\n");
    data_fd = open("/home/justin/rpi-rgb-led-matrix/examples-api-use/data_py_to_cc",O_RDONLY|O_NONBLOCK);
    printf("Data fifo opened on C++\n");

    printf("C++ sending command for baseball data\n");
    write(cmd_fd, cmd_tx, sizeof(cmd_tx));

    printf("Reading from data fifo\n");

    usleep(500*1000); // 50ms
    read(data_fd, data_rx, 1024);
    printf("Received %s", data_rx);

    // Get the game state - first item in the string
    char *token;
    // Split string into array of strings
    token = strtok(data_rx,",");

    if(token != NULL){
      std::string data(token);
      game_state = data;
      token = strtok(NULL,",");
    } else {
      // Set the game state to no game
      game_state = "nogame";
    }

    // PARSING THE DATA INTO C++ PROCESS
    // All game states (besides nogame) have team-specific data in the first six items
    // of the data, so we can process that outside of the "if" statements before processing
    // the rest of the specific data related to each game state

    // PARSING TEAM SPECIFIC DATA
    if(game_state != "nogame") {
      for (int i = 0; i<6; i++) {
        int color;

        if(token != NULL) {
          std::string data(token);
          switch(i) {
            case 0:
              // Check if it is a new day (new teams)
              if(away_abrv == data){
                new_teams = false;
              } else {
                new_teams = true;
              }
              away_abrv = data;
              break;
            case 1:
              color = std::stoi(data, NULL, 16);
              away_main.r = (color >> 16) & 0xFF;
              away_main.g = (color >> 8) & 0xFF;
              away_main.b = color & 0xFF;
              break;
            case 2:
              color = std::stoi(data, NULL, 16);
              away_second.r = (color >> 16) & 0xFF;
              away_second.g = (color >> 8) & 0xFF;
              away_second.b = color & 0xFF;
              break;
            case 3:
              if(home_abrv == data){
                new_teams = false;
              } else {
                new_teams = true;
              }
              home_abrv = data;
              break;
            case 4:
              color = std::stoi(data, NULL, 16);
              home_main.r = (color >> 16) & 0xFF;
              home_main.g = (color >> 8) & 0xFF;
              home_main.b = color & 0xFF;
              break;
            case 5:
              color = std::stoi(data, NULL, 16);
              home_second.r = (color >> 16) & 0xFF;
              home_second.g = (color >> 8) & 0xFF;
              home_second.b = color & 0xFF;
              break;
          }
          token = strtok(NULL,",");
        } else i=6;
      }
    }

    if(new_teams){
      // If the teams have changed, then update the file paths
      // and update the logos

      home_logo_fn = mlb_fn + home_abrv + ".png";
      away_logo_fn = mlb_fn + away_abrv + ".png";

      const char *home_fn = home_logo_fn.c_str();
      const char *away_fn = away_logo_fn.c_str();

      printf("home = %s\n away = %s\n", home_fn,away_fn);

      home_logo = LoadImageAndScaleImage(home_fn,
                                         logo_size,
                                         logo_size);
      away_logo = LoadImageAndScaleImage(away_fn,
                                         logo_size,
                                         logo_size);
      printf("new teams updated\n");

    }

    // TWO PARTS HERE: parse the rest of the data AND update the display
    // depending on the state of the game, or if there is a game
    if(game_state == "pre") {
      // Data in pre: first pitch time
      for(int i = 0; i<2; i++) {
      // PARSING
        if(token != NULL) {
          std::string data(token);
          switch(i){
            case 0:
              first_pitch_time = data;
              break;
            case 1:
              am_pm = data;
              break;
          }
          token = strtok(NULL,",");
        } else i=2;
      }

      home_runs = 0;
      away_runs = 0;
      home_score = std::to_string(home_runs);
      away_score = std::to_string(away_runs);

      // DISPLAY
      SetCanvasArea(offscreen_canvas, center_y, center_x, 28, 32, &blank);

      int first_pitch_x = 20;
      int first_pitch_y = 4;
      char first[] = "FIRST";
      char pitch[] = "PITCH";
      rgb_matrix::DrawText(offscreen_canvas, five_seven_font, first_pitch_x, first_pitch_y + five_seven_font.baseline(),
                           scoreboard_color, NULL, first, letter_spacing);
      rgb_matrix::DrawText(offscreen_canvas, five_seven_font, first_pitch_x, first_pitch_y+9 + five_seven_font.baseline(),
                           scoreboard_color, NULL, pitch, letter_spacing);
      rgb_matrix::DrawText(offscreen_canvas, five_seven_font, first_pitch_x, first_pitch_y+18 + five_seven_font.baseline(),
                           scoreboard_color, NULL, first_pitch_time.c_str(), letter_spacing);
      rgb_matrix::DrawText(offscreen_canvas, five_seven_font, first_pitch_x, first_pitch_y+27 + five_seven_font.baseline(),
                           scoreboard_color, NULL, am_pm.c_str(), letter_spacing);

      // -1 to give padding around logo
      SetCanvasArea(offscreen_canvas, home_x-1, home_y-1, 18, 32, &home_main);
      SetCanvasArea(offscreen_canvas, away_x-1, away_y-1, 18, 32, &away_main);
//printf("TRYING TO PRINT IMAGES\n");
      CopyImageToCanvasTransparent(home_logo[0], offscreen_canvas, &home_x, &home_y);
      CopyImageToCanvasTransparent(away_logo[0], offscreen_canvas, &away_x, &away_y);
//printf("PRINTED IMAGS\n");
      rgb_matrix::DrawText(offscreen_canvas, time_font, home_x+5, home_y+18 + time_font.baseline(),
                           home_second, NULL, home_score.c_str(), letter_spacing);

      rgb_matrix::DrawText(offscreen_canvas, time_font, away_x+5, away_y+18 + time_font.baseline(),
                           away_second, NULL, away_score.c_str(), letter_spacing);


    } else if(game_state == "in") {
      // data in in: balls, strikes, outs, onFirst, onSecond, onThird,
      // away_runs, home_runs, inning

      // PARSING
      for(int i=0; i<9; i++) {
        if(token!=NULL) {
          std::string data(token);
          switch(i){
            case 0:
              balls = stoi(data);
              break;
            case 1:
              strikes = stoi(data);
              break;
            case 2:
              outs = stoi(data);
              break;
            case 3:
              on_first = stoi(data);
              break;
            case 4:
              on_second = stoi(data);
              break;
            case 5:
              on_third = stoi(data);
              break;
            case 6:
              away_score = data;
              break;
            case 7:
              home_score = data;
              break;
            case 8:
              inning = data;
              break;
          }
          token = strtok(NULL,",");
        } else i=9;
      }

      // DISPLAY

      // -1 to give padding around logo
      SetCanvasArea(offscreen_canvas, home_x-1, home_y-1, 18, 32, &home_main);
      SetCanvasArea(offscreen_canvas, away_x-1, away_y-1, 18, 32, &away_main);

      CopyImageToCanvasTransparent(home_logo[0], offscreen_canvas, &home_x, &home_y);
      CopyImageToCanvasTransparent(away_logo[0], offscreen_canvas, &away_x, &away_y);

      rgb_matrix::DrawText(offscreen_canvas, time_font, home_x+5, home_y+18 + time_font.baseline(),
                           home_second, NULL, home_score.c_str(), letter_spacing);

      rgb_matrix::DrawText(offscreen_canvas, time_font, away_x+5, away_y+18 + time_font.baseline(),
                           away_second, NULL, away_score.c_str(), letter_spacing);

      // Middle scoreboard
      // BASES
      if(on_first) {
        CopyImageToCanvasTransparent(full_base[0], offscreen_canvas, &first_base_x, &first_base_y);
      } else {
        CopyImageToCanvasTransparent(empty_base[0], offscreen_canvas, &first_base_x, &first_base_y);
      }
      if(on_second) {
        CopyImageToCanvasTransparent(full_base[0], offscreen_canvas, &second_base_x, &second_base_y);
      } else {
        CopyImageToCanvasTransparent(empty_base[0], offscreen_canvas, &second_base_x, &second_base_y);
      }
      if(on_third) {
        CopyImageToCanvasTransparent(full_base[0], offscreen_canvas, &third_base_x, &third_base_y);
      } else {
        CopyImageToCanvasTransparent(empty_base[0], offscreen_canvas, &third_base_x, &third_base_y);
      }

      // INNINGS
      CopyImageToCanvasTransparent(top_inning[0], offscreen_canvas, &inning_x, &top_inning_y);
      CopyImageToCanvasTransparent(bot_inning[0], offscreen_canvas, &inning_x, &bot_inning_y);

      rgb_matrix::DrawText(offscreen_canvas, six_ten_font, inning_x, top_inning_y+6 + date_font.baseline(),
                           scoreboard_color, NULL, inning.c_str(), letter_spacing);

      // BALL
      rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, ball_y + date_font.baseline(),
                           scoreboard_color, NULL, b, letter_spacing);

      int cir_pos_temp = first_cir_x;
      for(int i = 1; i<4; i++) {
        if(balls>=i){
          CopyImageToCanvas(ball_circle[0], offscreen_canvas, &cir_pos_temp, &ball_y);
        } else {
          CopyImageToCanvas(empty_circle[0], offscreen_canvas, &cir_pos_temp, &ball_y);
        }
        cir_pos_temp += 5;
      }
      //CopyImageToCanvasTransparent(ball_circle[0], offscreen_canvas, &first_cir_x, &ball_y);
      //CopyImageToCanvasTransparent(ball_circle[0], offscreen_canvas, &second_cir_x, &ball_y);
      //CopyImageToCanvasTransparent(empty_circle[0], offscreen_canvas, &third_cir_x, &ball_y);

      // STRIKE
      rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, strike_y + date_font.baseline(),
                           scoreboard_color, NULL, s, letter_spacing);

      cir_pos_temp = first_cir_x;
      for(int i = 1; i<3; i++) {
        if(strikes>=i){
          CopyImageToCanvas(strike_circle[0], offscreen_canvas, &cir_pos_temp, &strike_y);
        } else {
          CopyImageToCanvas(empty_circle[0], offscreen_canvas, &cir_pos_temp, &strike_y);
        }
        cir_pos_temp += 5;
      }
      //CopyImageToCanvasTransparent(strike_circle[0], offscreen_canvas, &first_cir_x, &strike_y);
      //CopyImageToCanvasTransparent(empty_circle[0], offscreen_canvas, &second_cir_x, &strike_y);

      // OUT
      rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, out_y + date_font.baseline(),
                           scoreboard_color, NULL, o, letter_spacing);

      cir_pos_temp = first_cir_x;
      for(int i = 1; i<3; i++) {
        if(outs>=i){
          CopyImageToCanvas(out_circle[0], offscreen_canvas, &cir_pos_temp, &out_y);
        } else {
          CopyImageToCanvas(empty_circle[0], offscreen_canvas, &cir_pos_temp, &out_y);
        }
        cir_pos_temp += 5;
      }
      //CopyImageToCanvasTransparent(out_circle[0], offscreen_canvas, &first_cir_x, &out_y);
      //CopyImageToCanvasTransparent(out_circle[0], offscreen_canvas, &second_cir_x, &out_y);

    } else if(game_state == "post") {
      // data in postgame: away runs, home runs

      // PARSING
      for(int i=0; i<2; i++) {
        if(token!=NULL) {
          std::string data(token);
          switch(i){
            case 0:
              away_score = data;
              break;
            case 1:
              home_score = data;
              break;
          }
          token = strtok(NULL,",");
        } else i=2;
      }

      // DISPLAY
      SetCanvasArea(offscreen_canvas, center_y, center_x, 28, 32, &blank);

      int final_x = 20;
      int final_y = 4;
      char final[] = "FINAL";
      rgb_matrix::DrawText(offscreen_canvas, five_seven_font, final_x, final_y + five_seven_font.baseline(),
                           scoreboard_color, NULL, final, letter_spacing);

      // -1 to give padding around logo
      SetCanvasArea(offscreen_canvas, home_x-1, home_y-1, 18, 32, &home_main);
      SetCanvasArea(offscreen_canvas, away_x-1, away_y-1, 18, 32, &away_main);

      CopyImageToCanvasTransparent(home_logo[0], offscreen_canvas, &home_x, &home_y);
      CopyImageToCanvasTransparent(away_logo[0], offscreen_canvas, &away_x, &away_y);

      rgb_matrix::DrawText(offscreen_canvas, time_font, home_x+5, home_y+18 + time_font.baseline(),
                           home_second, NULL, home_score.c_str(), letter_spacing);

      rgb_matrix::DrawText(offscreen_canvas, time_font, away_x+5, away_y+18 + time_font.baseline(),
                           away_second, NULL, away_score.c_str(), letter_spacing);

    } else if(game_state == "nogame") {
      printf("nogame");
    }

/*
    // -1 to give padding around logo
    SetCanvasArea(offscreen_canvas, home_x-1, home_y-1, 18, 32, &home_main);
    SetCanvasArea(offscreen_canvas, away_x-1, away_y-1, 18, 32, &away_main);

    CopyImageToCanvasTransparent(home_logo[0], offscreen_canvas, &home_x, &home_y);
    CopyImageToCanvasTransparent(away_logo[0], offscreen_canvas, &away_x, &away_y);

    rgb_matrix::DrawText(offscreen_canvas, time_font, home_x+1, home_y+24 + date_font.baseline(),
                         home_second, NULL, home_score.c_str(), letter_spacing);

    rgb_matrix::DrawText(offscreen_canvas, time_font, away_x+5, away_y+24 + date_font.baseline(),
                         away_second, NULL, away_score.c_str(), letter_spacing);

    // Middle scoreboard
    CopyImageToCanvasTransparent(full_base[0], offscreen_canvas, &first_base_x, &first_base_y);
    CopyImageToCanvasTransparent(empty_base[0], offscreen_canvas, &second_base_x, &second_base_y);
    CopyImageToCanvasTransparent(full_base[0], offscreen_canvas, &third_base_x, &third_base_y);

    CopyImageToCanvasTransparent(top_inning[0], offscreen_canvas, &inning_x, &top_inning_y);
    CopyImageToCanvasTransparent(bot_inning[0], offscreen_canvas, &inning_x, &bot_inning_y);

    rgb_matrix::DrawText(offscreen_canvas, six_ten_font, inning_x, top_inning_y+6 + date_font.baseline(),
                         scoreboard_color, NULL, inning.c_str(), letter_spacing);

    // BALL
    rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, ball_y + date_font.baseline(),
                         scoreboard_color, NULL, b, letter_spacing);

    CopyImageToCanvasTransparent(ball_circle[0], offscreen_canvas, &first_cir_x, &ball_y);
    CopyImageToCanvasTransparent(ball_circle[0], offscreen_canvas, &second_cir_x, &ball_y);
    CopyImageToCanvasTransparent(empty_circle[0], offscreen_canvas, &third_cir_x, &ball_y);

    // STRIKE
    rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, strike_y + date_font.baseline(),
                         scoreboard_color, NULL, s, letter_spacing);

    CopyImageToCanvasTransparent(strike_circle[0], offscreen_canvas, &first_cir_x, &strike_y);
    CopyImageToCanvasTransparent(empty_circle[0], offscreen_canvas, &second_cir_x, &strike_y);

    // OUT
    rgb_matrix::DrawText(offscreen_canvas, date_font, circles_text_x, out_y + date_font.baseline(),
                         scoreboard_color, NULL, o, letter_spacing);

    CopyImageToCanvasTransparent(out_circle[0], offscreen_canvas, &first_cir_x, &out_y);
    CopyImageToCanvasTransparent(out_circle[0], offscreen_canvas, &second_cir_x, &out_y);
*/
    canvas->SwapOnVSync(offscreen_canvas);
    pthread_mutex_unlock(&canvas_mutex);
    usleep(10*1000*1000); // sleep 10s
  }
  printf("Exited baseball thread\n");
}