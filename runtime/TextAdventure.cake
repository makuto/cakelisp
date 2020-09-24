;; Cake Adventure
;; This is meant to be a dead simple "game" which you can modify while it is running.
;; This is to test hot-reloading

;; (import &comptime-only "Macros.cake")

(c-import "<stdio.h>"
          "cctype" ;; For isdigit
          "<stdlib.h>" ;; atoi
          &with-decls "<vector>")

(defstruct room
  name (* (const char))
  description (* (const char))
  connected-rooms (<> std::vector room))

(var rooms (const ([] room))
     (array
      (array "front porch" "You're outside the front door of your home. The lights are off inside."
             (array (array
                     "inside home" "Surprise! Your home is filled with cake. You look at your hands. You are cake."
                     (array))))))

(defun print-help ()
  (printf "At any point, enter 'r' to reload the code, 'h' for help, or 'q' to quit\n")
  (printf "Enter room number for desired room\n\n"))

(defun print-room (room-to-print (* (const room)))
  (printf "You are at: %s.\n%s\n"
          (path room-to-print > name)
          (path room-to-print > description))
  (var room-index int 0)
  (for-in connected-room (& (const room)) (path room-to-print > connected-rooms)
          (printf "%d: %s\n" room-index (field connected-room name))
          (incr room-index))
  (when (on-call (path room-to-print > connected-rooms) empty)
    (printf "There are no connected rooms. This must be the end of the game!\n")))

;; Return true to hot-reload, or false to exit
(defun reloadable-entry-point (&return bool)
  (printf "CAKE ADVENTURE\n\n")
  (print-help)

  (var current-room (* (const room)) (addr (at 0 rooms)))
  (print-room current-room)

  (var input char 0)
  (var previous-room (* (const room)) current-room)
  (while (!= input 'q')
    (when (!= current-room previous-room)
      (set previous-room current-room)
      (print-room current-room))

    (printf "> ")
    (set input (getchar))

    (when (= input 'r')
      (return true))
    (when (= input 'h')
      (print-help))
    (when (call (in std isdigit) input)
      (var room-request int (atoi (addr input)))
      (var num-rooms int (on-call (path current-room > connected-rooms) size))
      (unless (and num-rooms (< room-request num-rooms))
        (printf "I don't know where that is. There are %d rooms\n" num-rooms)
        (continue))
      (printf "Going to room %d\n" room-request)
      (set current-room (addr (at room-request
                                  (path current-room > connected-rooms))))))

  (return false))
