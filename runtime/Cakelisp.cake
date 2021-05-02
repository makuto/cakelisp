;; An interface for accessing Cakelisp itself at comptime, e.g. for running Cakelisp as a sub-step
(import &comptime-only "CHelpers.cake")


(defmacro cakelisp-manager-destroy-and (module-manager any &rest body any)
  (tokenize-push output
    ;; (Log "--------------------- } Closed Cakelisp sub-instance\n")
    (moduleManagerDestroyKeepDynLibs module-manager)
    (token-splice-rest body tokens))
  (return true))

(defun-comptime cakelisp-evaluate-build-files-internal
    (module-manager (& ModuleManager)
     files (* (* (const char))) num-files int
     build-outputs (& (<> (in std vector) (in std string)))
     &return bool)
  (each-in-range num-files i
    (unless (moduleManagerAddEvaluateFile module-manager (at i files) null)
      (return false)))

  (unless (moduleManagerEvaluateResolveReferences module-manager)
    (return false))
  (unless (moduleManagerWriteGeneratedOutput module-manager)
    (return false))

  (unless (moduleManagerBuildAndLink module-manager build-outputs)
    (return false))
  (return true))

(defun-comptime cakelisp-evaluate-build-files (files (* (* (const char))) num-files int
                                               &return bool)
  ;; (Log "--------------------- { Open Cakelisp sub-instance\n");
  (var module-manager ModuleManager (array))
  (moduleManagerInitialize module-manager)
  (var build-outputs (<> (in std vector) (in std string)))
  (unless (cakelisp-evaluate-build-files-internal module-manager files num-files build-outputs)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (cakelisp-manager-destroy-and module-manager (return true)))

(defun-comptime cakelisp-evaluate-build-execute-files (files (* (* (const char))) num-files int
                                                       &return bool)
  ;; (Log "--------------------- { Open Cakelisp sub-instance\n");
  (var module-manager ModuleManager (array))
  (moduleManagerInitialize module-manager)
  (var build-outputs (<> (in std vector) (in std string)))
  (unless (cakelisp-evaluate-build-files-internal module-manager files num-files build-outputs)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (unless (moduleManagerExecuteBuiltOutputs module-manager build-outputs)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (cakelisp-manager-destroy-and module-manager (return true)))
