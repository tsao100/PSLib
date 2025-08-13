      PROGRAM PSLIB_TEST
        EXTERNAL START_GUI, REGISTER_ACTION
        EXTERNAL ON_LINE

        CALL REGISTER_ACTION('LINE', ON_LINE)

        CALL START_GUI
      END

      SUBROUTINE ON_LINE
        INTEGER X1, Y1, X2, Y2
  
        PRINT *, 'Click first point...'
        CALL PS_GETPOINT('Click first point...', X1, Y1, 0)
        X2 = X1
        Y2 = Y1
        PRINT *, 'Click second point...'
        CALL PS_GETPOINT('Click second point...', X2, Y2, 1)
  
        CALL PS_DRAW_LINE(X1, Y1, X2, Y2)
  
        PRINT *, 'Line drawn.'

      END
