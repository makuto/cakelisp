;; Cake Adventure
;; This is meant to be a dead simple "game" which you can modify while it is running.
;; This is to test hot-reloading

;; (import &comptime-only "ComptimeHelpers.cake")

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

(var num-times-loaded int 0)

(defun print-help ()
  (fprintf stderr "At any point, enter 'r' to reload the code, 'h' for help, or 'q' to quit\n")
  (fprintf stderr "Enter room number for desired room\n\n"))

(defun print-room (room-to-print (* (const room)))
  (fprintf stderr "You are at: %s.\n%s\n"
          (path room-to-print > name)
          (path room-to-print > description))
  (var room-index int 0)
  (for-in connected-room (& (const room)) (path room-to-print > connected-rooms)
          (fprintf stderr "%d: %s\n" room-index (field connected-room name))
          (incr room-index))
  (when (call-on empty (path room-to-print > connected-rooms))
    (fprintf stderr "There are no connected rooms. This must be the end of the game!\n")))

;; Return true to hot-reload, or false to exit
(defun reloadable-entry-point (&return bool)
  (fprintf stderr "CAKE ADVENTURE\n\n")
  (print-help)

  (var current-room (* (const room)) (addr (at 0 rooms)))
  (print-room current-room)

  (incr num-times-loaded)
  (fprintf stderr "Loaded %d times\n" num-times-loaded)

  (var input char 0)
  (var previous-room (* (const room)) current-room)
  (while (!= input 'q')
    (when (!= current-room previous-room)
      (set previous-room current-room)
      (print-room current-room))

    (fprintf stderr "> ")
    (set input (getchar))

    (cond
      ((= input 'r')
       (return true))
      ((= input 'h')
       (print-help))
      ((call (in std isdigit) input)
       (var room-request int (atoi (addr input)))
       (var num-rooms int (call-on size (path current-room > connected-rooms)))
       (unless (and num-rooms (< room-request num-rooms))
         (fprintf stderr "I don't know where that is. There are %d rooms\n" num-rooms)
         (continue))
       (fprintf stderr "Going to room %d\n" room-request)
       (set current-room (addr (at room-request
                                   (path current-room > connected-rooms)))))
      (true
       (print-help))))

  (return false))
