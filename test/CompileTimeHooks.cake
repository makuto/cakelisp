(defun main (&return int)
  (return 0))

(defun-comptime third-hook (manager (& ModuleManager) module (* Module) &return bool)
  (Log "I'm #3\n")
  (return true))

(defun-comptime second-hook (manager (& ModuleManager) module (* Module) &return bool)
  (Log "I'm #2\n")
  (return true))

(defun-comptime first-hook (manager (& ModuleManager) module (* Module) &return bool)
  (Log "I'm #1\n")
  (return true))

(defun-comptime last-hook (manager (& ModuleManager) module (* Module) &return bool)
  (Log "I'm last\n")
  (return true))

(add-compile-time-hook-module pre-build first-hook)
(add-compile-time-hook-module pre-build third-hook :priority-decrease 3)
(add-compile-time-hook-module pre-build last-hook :priority-decrease 10000)
(add-compile-time-hook-module pre-build second-hook :priority-decrease 2)

(set-cakelisp-option executable-output "test/Hooks")
