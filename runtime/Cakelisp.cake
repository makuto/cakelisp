;; An interface for accessing Cakelisp itself at comptime, e.g. for running Cakelisp as a sub-step
(import &comptime-only "CHelpers.cake")

(defmacro cakelisp-manager-destroy-and (module-manager any &rest body any)
  (tokenize-push output
    (moduleManagerDestroyKeepDynLibs module-manager)
    ;; (Log "--------------------- } Closed Cakelisp sub-instance\n")
    (token-splice-rest body tokens))
  (return true))

(defun-comptime cakelisp-evaluate-build-execute-files (files (* (* (const char))) num-files int
                                                       &return bool)
  ;; (Log "--------------------- { Open Cakelisp sub-instance\n");
  (var module-manager ModuleManager (array))
  (moduleManagerInitialize module-manager)

  (each-in-range num-files i
    (unless (moduleManagerAddEvaluateFile module-manager (at i files) null)
      (cakelisp-manager-destroy-and module-manager (return false))))

  (unless (moduleManagerEvaluateResolveReferences module-manager)
    (cakelisp-manager-destroy-and module-manager (return false)))
  (unless (moduleManagerWriteGeneratedOutput module-manager)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (var build-outputs (<> (in std vector) (in std string)))
  (unless (moduleManagerBuildAndLink module-manager build-outputs)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (unless (moduleManagerExecuteBuiltOutputs module-manager build-outputs)
    (cakelisp-manager-destroy-and module-manager (return false)))

  (cakelisp-manager-destroy-and module-manager (return true)))
