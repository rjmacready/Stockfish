(define-module (userscripts)
  #:export (evaluate))


(define (__evaluate pos v)
  (+ (- (random 1000) 500) v))

(define (_evaluate pos v)
  v)

(define (evaluate pos v)
  (if (= (side-to-move pos) 0)
      v
      (* -1 v)))

